#include "dsp/Dynamics.h"

#include <algorithm>
#include <cmath>

namespace pf8::dsp {

// ---------------------------------------------------------------------------------------------
// Noise gate: peak envelope (instant attack, 20 ms decay) on a 100 Hz high-passed sidechain,
// 3 dB hysteresis, hold, then a linear-domain gain glide with attack/release time constants.

void NoiseGate::prepare(double fs)
{
    fs_ = fs;
    sidechainHp_.setCoeffs(BiquadCoeffs::highPass(fs, 100.0, 0.7071));
    reset();
}

void NoiseGate::reset()
{
    sidechainHp_.reset();
    env_ = 0.0f;
    gain_ = 1.0f;
    holdLeft_ = 0;
    open_ = true;
}

void NoiseGate::process(float* x, int n) noexcept
{
    const float openThr = dbToLin(s_.thresholdDb);
    const float closeThr = dbToLin(s_.thresholdDb - 3.0f);
    const float floorGain = dbToLin(std::min(0.0f, s_.rangeDb));
    const float envDecay = timeCoeff(fs_, 20.0);
    const float att = timeCoeff(fs_, std::max(0.1f, s_.attackMs));
    const float rel = timeCoeff(fs_, std::max(1.0f, s_.releaseMs));
    const int hold = static_cast<int>(fs_ * s_.holdMs * 0.001);
    for (int i = 0; i < n; ++i)
    {
        const float sc = std::abs(sidechainHp_.process(x[i]));
        env_ = sc > env_ ? sc : env_ * envDecay;
        if (env_ >= openThr)
        {
            open_ = true;
            holdLeft_ = hold;
        }
        else if (open_ && env_ < closeThr)
        {
            if (holdLeft_ > 0) --holdLeft_;
            else open_ = false;
        }
        const float target = open_ ? 1.0f : floorGain;
        const float c = target > gain_ ? att : rel;
        gain_ = target + (gain_ - target) * c;
        x[i] *= gain_;
    }
}

// ---------------------------------------------------------------------------------------------
// Compressor: feed-forward, log domain, soft knee; ballistics on the gain-reduction signal.

void Compressor::prepare(double fs)
{
    fs_ = fs;
    reset();
}

void Compressor::reset()
{
    rms_ = peak_ = 0.0f;
    grDb_ = 0.0f;
}

float Compressor::staticGainDb(float x, float T, float R, float W) noexcept
{
    const float slope = 1.0f / std::max(1.0f, R) - 1.0f; // ≤ 0
    if (W > 0.0f && std::abs(x - T) <= W * 0.5f)
    {
        const float d = x - T + W * 0.5f;
        return slope * d * d / (2.0f * W);
    }
    return x > T ? slope * (x - T) : 0.0f;
}

float Compressor::makeupDb() const noexcept
{
    if (!s_.autoMakeup) return s_.makeupDb;
    // Half the reduction a 0 dBFS signal would get: a conservative, level-independent estimate.
    return -0.5f * staticGainDb(0.0f, s_.thresholdDb, s_.ratio, s_.kneeDb);
}

void Compressor::process(float* x, int n) noexcept
{
    const float rmsCoef = timeCoeff(fs_, 10.0);
    const float att = timeCoeff(fs_, std::max(0.1f, s_.attackMs));
    const float rel = timeCoeff(fs_, std::max(1.0f, s_.releaseMs));
    const float makeup = dbToLin(makeupDb());
    for (int i = 0; i < n; ++i)
    {
        float levelDb;
        if (s_.detector == DetectorMode::Rms)
        {
            rms_ = rmsCoef * rms_ + (1.0f - rmsCoef) * x[i] * x[i];
            levelDb = 10.0f * std::log10(rms_ + 1e-20f) + 3.0103f; // sine-calibrated RMS → peak-equivalent dB
        }
        else
        {
            const float a = std::abs(x[i]);
            peak_ = a > peak_ ? a : peak_ * rmsCoef;
            levelDb = linToDb(peak_);
        }
        const float target = staticGainDb(levelDb, s_.thresholdDb, s_.ratio, s_.kneeDb);
        const float c = target < grDb_ ? att : rel; // more reduction = attack
        grDb_ = target + (grDb_ - target) * c;
        x[i] *= dbToLin(grDb_) * makeup;
    }
}

// ---------------------------------------------------------------------------------------------
// De-esser: band-pass sidechain at the sibilance frequency; when above threshold, a dynamic peak
// cut at that frequency reduces only the sibilant band, proportionally to the overshoot.

void DeEsser::prepare(double fs)
{
    fs_ = fs;
    lastFreq_ = 0.0f;
    reset();
}

void DeEsser::reset()
{
    detector_.reset();
    cut_.reset();
    env_ = 0.0f;
    reductionDb_ = appliedDb_ = 0.0f;
}

void DeEsser::process(float* x, int n) noexcept
{
    if (s_.freq != lastFreq_)
    {
        detector_.setCoeffs(BiquadCoeffs::bandPass(fs_, s_.freq, 2.0));
        lastFreq_ = s_.freq;
    }
    const float att = timeCoeff(fs_, 1.0), rel = timeCoeff(fs_, 60.0);
    const float thr = s_.thresholdDb;
    constexpr int kSub = 16; // re-design the cut filter every 16 samples
    for (int start = 0; start < n; start += kSub)
    {
        const int len = std::min(kSub, n - start);
        for (int i = 0; i < len; ++i)
        {
            const float a = std::abs(detector_.process(x[start + i]));
            env_ = a > env_ ? a + (env_ - a) * att : a + (env_ - a) * rel;
        }
        const float over = linToDb(env_) - thr;
        reductionDb_ = std::clamp(over, 0.0f, std::max(0.0f, s_.amountDb));
        const BiquadCoeffs target = BiquadCoeffs::peak(fs_, s_.freq, 2.0, -reductionDb_);
        cut_.processInterpolated(x + start, len, target);
        appliedDb_ = reductionDb_;
    }
}

// ---------------------------------------------------------------------------------------------
// Limiter

void Limiter::prepare(double fs, int channels, int maxBlock, double lookaheadMs, double releaseMs)
{
    (void)maxBlock;
    channels_ = std::clamp(channels, 1, 2);
    lookahead_ = std::max(1, static_cast<int>(fs * lookaheadMs * 0.001));
    release_ = timeCoeff(fs, releaseMs);
    delay_.assign(static_cast<size_t>(channels_) * lookahead_, 0.0f);
    qCap_ = lookahead_ + 2;
    qIdx_.assign(static_cast<size_t>(qCap_), 0);
    qVal_.assign(static_cast<size_t>(qCap_), 1.0f);
    avgRing_.assign(static_cast<size_t>(lookahead_ + 1), 1.0f);
    reset();
}

void Limiter::reset()
{
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    std::fill(avgRing_.begin(), avgRing_.end(), 1.0f);
    avgSum_ = static_cast<double>(avgRing_.size());
    avgPos_ = 0;
    delayPos_ = 0;
    qHead_ = qCount_ = 0;
    counter_ = 0;
    gain_ = lastGain_ = 1.0f;
    for (auto& h : tpHist_)
        for (float& v : h) v = 0.0f;
}

float Limiter::requirement(int i, float* const* x) noexcept
{
    float peak = 0.0f;
    for (int c = 0; c < channels_; ++c)
    {
        const float s = x[c][i];
        float p = std::abs(s);
        if (truePeak_)
        {
            // 4× oversampled peak estimate between the previous and current sample (4-tap cubic
            // Hermite per phase — catches inter-sample overs a sample-peak meter misses).
            float* h = tpHist_[c];
            h[0] = h[1];
            h[1] = h[2];
            h[2] = h[3];
            h[3] = s;
            for (int ph = 1; ph < 4; ++ph)
            {
                const float t = static_cast<float>(ph) * 0.25f;
                const float c0 = h[1], c1 = 0.5f * (h[2] - h[0]);
                const float c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3];
                const float c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
                p = std::max(p, std::abs(((c3 * t + c2) * t + c1) * t + c0));
            }
        }
        peak = std::max(peak, p);
    }
    return peak > ceiling_ ? ceiling_ / peak : 1.0f;
}

void Limiter::process(float* const* x, int n, bool enabled) noexcept
{
    const int L = lookahead_;
    const int ring = L + 1;
    for (int i = 0; i < n; ++i)
    {
        // 1. Requirement for the newest sample → sliding minimum over samples [t−L, t].
        const float req = enabled ? requirement(i, x) : 1.0f;
        while (qCount_ > 0)
        {
            const int back = (qHead_ + qCount_ - 1) % qCap_;
            if (qVal_[static_cast<size_t>(back)] >= req) --qCount_;
            else break;
        }
        {
            const int slot = (qHead_ + qCount_) % qCap_;
            qIdx_[static_cast<size_t>(slot)] = counter_;
            qVal_[static_cast<size_t>(slot)] = req;
            ++qCount_;
        }
        while (qIdx_[static_cast<size_t>(qHead_)] < counter_ - L)
        {
            qHead_ = (qHead_ + 1) % qCap_;
            --qCount_;
        }
        const float windowMin = qVal_[static_cast<size_t>(qHead_)];

        // 2. Box average of the window minimum over L+1 samples (smooth attack, still ≤ requirement).
        avgSum_ += windowMin - avgRing_[static_cast<size_t>(avgPos_)];
        avgRing_[static_cast<size_t>(avgPos_)] = windowMin;
        avgPos_ = (avgPos_ + 1) % ring;
        const float smoothed = static_cast<float>(avgSum_ / ring);

        // 3. Release: rise slowly, never above the allowed value.
        gain_ = smoothed < gain_ ? smoothed : smoothed + (gain_ - smoothed) * release_;
        gain_ = std::min(gain_, smoothed);

        // 4. Apply to the sample delayed by exactly L (its requirement is inside the window).
        for (int c = 0; c < channels_; ++c)
        {
            float* d = delay_.data() + static_cast<size_t>(c) * L;
            const float delayed = d[delayPos_];
            d[delayPos_] = x[c][i];
            float y = delayed * gain_; // disabled: requirement 1, so the gain releases smoothly to unity
            if (enabled) y = std::clamp(y, -ceiling_, ceiling_); // float-rounding safety
            x[c][i] = y;
        }
        delayPos_ = (delayPos_ + 1) % L;
        ++counter_;
    }
    lastGain_ = gain_;
}

} // namespace pf8::dsp
