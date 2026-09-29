#include "dsp/ChannelStrip.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pf8::dsp {

void ChannelStrip::prepare(double fs, int maxBlock)
{
    fs_ = fs;
    maxBlock_ = maxBlock;
    wet_.assign(static_cast<size_t>(maxBlock), 0.0f);
    gate_.prepare(fs);
    deesser_.prepare(fs);
    comp_.prepare(fs);
    limiter_.prepare(fs, 1, maxBlock, 1.5, 50.0);
    fadeStep_ = static_cast<float>(1.0 / (fs * 0.010)); // 10 ms crossfades
    reset();
}

void ChannelStrip::reset()
{
    for (auto& h : hpf_) h.reset();
    for (auto& e : eq_) e.reset();
    hpfHz_ = -1.0f;
    eqDesign_.fill(EqDesign{});
    gate_.reset();
    deesser_.reset();
    comp_.reset();
    limiter_.reset();
    trimGain_ = 1.0f;
    polarity_ = 1.0f;
}

void ChannelStrip::setTarget(Blend& b, bool on) noexcept
{
    b.target = on ? 1.0f : 0.0f;
    b.step = fadeStep_;
}

template <typename F>
void ChannelStrip::runStage(Blend& b, float* x, int n, F&& fn) noexcept
{
    if (!b.active()) return;
    const bool steadyOn = b.mix >= 1.0f && b.target >= 1.0f;
    if (steadyOn)
    {
        fn(x, n);
        return;
    }
    std::memcpy(wet_.data(), x, sizeof(float) * static_cast<size_t>(n));
    fn(wet_.data(), n);
    // Raised-cosine (S-curve) crossfade: zero slope at both ends, so the level change has no corner.
    for (int i = 0; i < n; ++i)
    {
        if (b.mix < b.target) b.mix = std::min(b.target, b.mix + b.step);
        else if (b.mix > b.target) b.mix = std::max(b.target, b.mix - b.step);
        const float s = 0.5f - 0.5f * std::cos(3.14159265f * b.mix);
        x[i] += (wet_[static_cast<size_t>(i)] - x[i]) * s;
    }
}

void ChannelStrip::process(float* x, int n, const ChannelDspParams& p) noexcept
{
    // Trim and polarity (ramped across the block).
    const float trimTarget = dbToLin(std::clamp(p.inputTrimDb.get(), -24.0f, 24.0f));
    const float polTarget = p.polarityInvert.load(std::memory_order_relaxed) ? -1.0f : 1.0f;
    if (trimTarget != trimGain_ || polTarget != polarity_)
    {
        const float g0 = trimGain_ * polarity_, g1 = trimTarget * polTarget;
        for (int i = 0; i < n; ++i) x[i] *= g0 + (g1 - g0) * static_cast<float>(i + 1) / static_cast<float>(n);
        trimGain_ = trimTarget;
        polarity_ = polTarget;
    }
    else if (trimGain_ * polarity_ != 1.0f)
    {
        const float g = trimGain_ * polarity_;
        for (int i = 0; i < n; ++i) x[i] *= g;
    }

    // High-pass: Butterworth 12 dB (Q 0.707) or 24 dB (Q 0.541 + 1.307).
    setTarget(bHpf_, p.hpfOn.load(std::memory_order_relaxed));
    const float hz = std::clamp(p.hpfHz.get(), 20.0f, 300.0f);
    const bool is24 = p.hpf24dB.load(std::memory_order_relaxed);
    runStage(bHpf_, x, n, [&](float* y, int len) {
        if (hz != hpfHz_ || is24 != hpf24_)
        {
            const auto c0 = BiquadCoeffs::highPass(fs_, hz, is24 ? 0.5411961 : 0.7071068);
            const auto c1 = BiquadCoeffs::highPass(fs_, hz, 1.3065630);
            if (hpfHz_ < 0.0f)
            {
                hpf_[0].setCoeffs(c0);
                hpf_[1].setCoeffs(c1);
                hpf_[0].process(y, len);
                if (is24) hpf_[1].process(y, len);
            }
            else
            {
                hpf_[0].processInterpolated(y, len, c0);
                if (is24) hpf_[1].processInterpolated(y, len, c1);
                else hpf_[1].setCoeffs(c1);
            }
            hpfHz_ = hz;
            hpf24_ = is24;
        }
        else
        {
            hpf_[0].process(y, len);
            if (is24) hpf_[1].process(y, len);
        }
    });

    // Gate.
    setTarget(bGate_, p.gateOn.load(std::memory_order_relaxed));
    gate_.set({p.gateThresholdDb.get(), p.gateRangeDb.get(), p.gateAttackMs.get(), p.gateHoldMs.get(), p.gateReleaseMs.get()});
    runStage(bGate_, x, n, [&](float* y, int len) { gate_.process(y, len); });

    // EQ (4 bands).
    setTarget(bEq_, p.eqOn.load(std::memory_order_relaxed));
    runStage(bEq_, x, n, [&](float* y, int len) {
        for (size_t b = 0; b < 4; ++b)
        {
            const auto& bp = p.eq[b];
            if (!bp.on.load(std::memory_order_relaxed)) continue;
            const float f = std::clamp(bp.freq.get(), 20.0f, 20000.0f);
            const float gdb = std::clamp(bp.gainDb.get(), -18.0f, 18.0f);
            const float q = std::clamp(bp.q.get(), 0.1f, 10.0f);
            const auto type = bp.type.load(std::memory_order_relaxed);
            auto& d = eqDesign_[b];
            if (!d.valid || d.f != f || d.g != gdb || d.q != q || d.t != type)
            {
                const auto c = type == EqBandType::LowShelf ? BiquadCoeffs::lowShelf(fs_, f, q, gdb)
                             : type == EqBandType::HighShelf ? BiquadCoeffs::highShelf(fs_, f, q, gdb)
                                                             : BiquadCoeffs::peak(fs_, f, q, gdb);
                if (!d.valid) { eq_[b].setCoeffs(c); eq_[b].process(y, len); }
                else eq_[b].processInterpolated(y, len, c);
                d = EqDesign{f, gdb, q, type, true};
            }
            else
                eq_[b].process(y, len);
        }
    });

    // De-esser.
    setTarget(bDeess_, p.deesserOn.load(std::memory_order_relaxed));
    deesser_.set({std::clamp(p.deessFreq.get(), 3000.0f, 12000.0f), p.deessThresholdDb.get(), p.deessAmountDb.get()});
    runStage(bDeess_, x, n, [&](float* y, int len) { deesser_.process(y, len); });

    // Compressor.
    setTarget(bComp_, p.compOn.load(std::memory_order_relaxed));
    comp_.set({p.compThresholdDb.get(), std::max(1.0f, p.compRatio.get()), p.compAttackMs.get(), p.compReleaseMs.get(),
               std::clamp(p.compKneeDb.get(), 0.0f, 12.0f), std::clamp(p.compMakeupDb.get(), 0.0f, 24.0f),
               p.compAutoMakeup.load(std::memory_order_relaxed), p.compDetector.load(std::memory_order_relaxed)});
    runStage(bComp_, x, n, [&](float* y, int len) { comp_.process(y, len); });

    // Limiter: always in the path for constant latency.
    limiter_.setCeilingDb(std::clamp(p.limiterCeilingDb.get(), -12.0f, 0.0f));
    float* chans[1] = {x};
    limiter_.process(chans, n, p.limiterOn.load(std::memory_order_relaxed));

    meters_.gateGain = bGate_.active() ? gate_.gain() : 1.0f;
    meters_.compGrDb = bComp_.active() ? comp_.gainReductionDb() : 0.0f;
    meters_.deessDb = bDeess_.active() ? deesser_.reductionDb() : 0.0f;
    meters_.limiterGrDb = limiter_.gainReductionDb();
}

} // namespace pf8::dsp
