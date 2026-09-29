#include "routing/RoutingEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pf8 {
namespace {

constexpr float kHalfPi = 1.57079632679489661923f;

int ms(double rate, double milliseconds) { return std::max(1, static_cast<int>(rate * milliseconds / 1000.0)); }

float* row(std::vector<float>& v, int r, int maxBlock) { return v.data() + static_cast<size_t>(r) * maxBlock; }

} // namespace

const char* toString(SourceId s) noexcept
{
    static const char* names[] = {"CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8", "Music", "Carts", "Talkback", "Remote"};
    const int i = idx(s);
    return i >= 0 && i < kSourceCount ? names[i] : "?";
}

const char* toString(BusId b) noexcept
{
    static const char* names[] = {"Main", "Clean", "Music", "HP1", "HP2", "HP3", "HP4", "HP5", "HP6", "HP7", "HP8", "PFL", "Monitor"};
    const int i = idx(b);
    return i >= 0 && i < kBusCount ? names[i] : "?";
}

const char* toString(HpMode m) noexcept
{
    switch (m)
    {
        case HpMode::Main: return "Main mix";
        case HpMode::Personal: return "Personal";
        case HpMode::Custom: return "Custom";
    }
    return "?";
}

void RoutingEngine::panLaw(float pan, float& l, float& r) noexcept
{
    // Constant power, −3 dB at centre.
    const float p = std::clamp(pan, -1.0f, 1.0f);
    const float a = (p + 1.0f) * 0.5f * kHalfPi;
    l = std::cos(a);
    r = std::sin(a);
}

void RoutingEngine::prepare(double sampleRate, int maxBlock)
{
    maxBlock_ = maxBlock;
    const size_t chBuf = static_cast<size_t>(kRoutingChannels) * maxBlock;
    post_.assign(chBuf, 0.0f);
    pre_.assign(chBuf, 0.0f);
    panLBuf_.assign(chBuf, 0.0f);
    panRBuf_.assign(chBuf, 0.0f);
    gainBuf_.assign(static_cast<size_t>(maxBlock), 0.0f);
    tmpL_.assign(static_cast<size_t>(maxBlock), 0.0f);
    tmpR_.assign(static_cast<size_t>(maxBlock), 0.0f);
    soloL_.assign(static_cast<size_t>(maxBlock), 0.0f);
    soloR_.assign(static_cast<size_t>(maxBlock), 0.0f);

    const int r10 = ms(sampleRate, 10), r5 = ms(sampleRate, 5), r20 = ms(sampleRate, 20);
    for (int ch = 0; ch < kRoutingChannels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        const auto& p = params_.channel[c];
        fader_[c].setLength(r10);
        fader_[c].reset(p.fader.get());
        mute_[c].setLength(r5);
        mute_[c].reset(p.mute.load() || !p.coughOpen.load() ? 0.0f : 1.0f);
        pfl_[c].setLength(r5);
        pfl_[c].reset(p.pfl.load() ? 1.0f : 0.0f);
        solo_[c].setLength(r5);
        solo_[c].reset(p.solo.load() ? 1.0f : 0.0f);
        float l, r;
        panLaw(p.pan.get(), l, r);
        pan_[c].left.setLength(r10);
        pan_[c].right.setLength(r10);
        pan_[c].left.reset(l);
        pan_[c].right.reset(r);
        const auto& hp = params_.headphones[c];
        hpVolume_[c].setLength(r10);
        hpVolume_[c].reset(hp.mute.load() ? 0.0f : hp.volume.get());
        hpTalkbackDim_[c].setLength(r5);
        hpTalkbackDim_[c].reset(1.0f);
        talkbackToHp_[c].setLength(r5);
        talkbackToHp_[c].reset(0.0f);
        hpMainness_[c].setLength(r20);
        hpMainness_[c].reset(hp.mode.load() == HpMode::Main ? 1.0f : 0.0f);
    }
    for (size_t s = 0; s < kSourceCount; ++s)
        for (size_t b = 0; b < kBusCount; ++b)
        {
            matrix_[s][b].setLength(r10);
            matrix_[s][b].reset(params_.gain[s][b].get());
        }
    masterGain_.setLength(r10);
    masterGain_.reset(params_.masterMute.load() ? 0.0f : params_.masterFader.get());
    talkbackProgram_.setLength(r5);
    talkbackProgram_.reset(0.0f);
    monitorGain_.setLength(r10);
    monitorGain_.reset(1.0f);
    soloBlend_.setLength(r5);
    soloBlend_.reset(0.0f);
}

void RoutingEngine::updateTargets() noexcept
{
    anySolo_ = false;
    anyPfl_ = false;
    for (int ch = 0; ch < kRoutingChannels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        const auto& p = params_.channel[c];
        fader_[c].setTarget(std::max(0.0f, p.fader.get()));
        mute_[c].setTarget(p.mute.load(std::memory_order_relaxed) || !p.coughOpen.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
        const bool pfl = p.pfl.load(std::memory_order_relaxed);
        const bool solo = p.solo.load(std::memory_order_relaxed);
        pfl_[c].setTarget(pfl ? 1.0f : 0.0f);
        solo_[c].setTarget(solo ? 1.0f : 0.0f);
        anyPfl_ |= pfl;
        anySolo_ |= solo;
        float l, r;
        panLaw(p.pan.get(), l, r);
        pan_[c].left.setTarget(l);
        pan_[c].right.setTarget(r);

        const auto& hp = params_.headphones[c];
        hpMode_[c] = hp.mode.load(std::memory_order_relaxed);
        hpMainness_[c].setTarget(hpMode_[c] == HpMode::Main ? 1.0f : 0.0f);
        hpVolume_[c].setTarget(hp.mute.load(std::memory_order_relaxed) ? 0.0f : std::max(0.0f, hp.volume.get()));

        const bool tbHere = params_.talkbackActive.load(std::memory_order_relaxed) &&
                            params_.talkbackTarget[c].load(std::memory_order_relaxed);
        talkbackToHp_[c].setTarget(tbHere ? params_.talkbackLevel.get() : 0.0f);
        hpTalkbackDim_[c].setTarget(tbHere && params_.talkbackDim.load(std::memory_order_relaxed) ? 0.25f : 1.0f);
    }
    for (size_t s = 0; s < kSourceCount; ++s)
        for (size_t b = 0; b < kBusCount; ++b) matrix_[s][b].setTarget(params_.gain[s][b].get());
    // The talkback lock: never on the program buses unless explicitly enabled.
    matrix_[static_cast<size_t>(idx(SourceId::Talkback))][idx(BusId::Main)].setTarget(0.0f);
    matrix_[static_cast<size_t>(idx(SourceId::Talkback))][idx(BusId::Clean)].setTarget(0.0f);
    const bool toProgram = params_.talkbackToProgram.load(std::memory_order_relaxed) &&
                           params_.talkbackActive.load(std::memory_order_relaxed);
    talkbackProgram_.setTarget(toProgram ? params_.talkbackLevel.get() : 0.0f);

    masterGain_.setTarget(params_.masterMute.load(std::memory_order_relaxed) ? 0.0f : std::max(0.0f, params_.masterFader.get()));

    const auto& m = params_.monitor;
    MonitorSource src = m.source.load(std::memory_order_relaxed);
    if (anyPfl_ && m.autoPfl.load(std::memory_order_relaxed)) src = MonitorSource::Pfl;
    monitorSource_ = src;
    float mg = m.mute.load(std::memory_order_relaxed) ? 0.0f : std::max(0.0f, m.volume.get());
    if (m.dim.load(std::memory_order_relaxed)) mg *= m.dimGain.get();
    monitorGain_.setTarget(mg);
    soloBlend_.setTarget(anySolo_ && src == MonitorSource::Main ? 1.0f : 0.0f);
}

void RoutingEngine::addMono(float* l, float* r, const float* src, Ramp& gain, const float* panL, const float* panR, int n) noexcept
{
    if (!src || (!gain.ramping() && gain.current() == 0.0f))
    {
        // Still advance the ramp so its state tracks time.
        if (gain.ramping()) gain.fill(gainBuf_.data(), n);
        return;
    }
    gain.fill(gainBuf_.data(), n);
    const float* g = gainBuf_.data();
    for (int i = 0; i < n; ++i)
    {
        const float v = src[i] * g[i];
        l[i] += v * panL[i];
        r[i] += v * panR[i];
    }
}

void RoutingEngine::addStereo(float* l, float* r, const float* sl, const float* sr, Ramp& gain, int n) noexcept
{
    if (!sl || (!gain.ramping() && gain.current() == 0.0f))
    {
        if (gain.ramping()) gain.fill(gainBuf_.data(), n);
        return;
    }
    gain.fill(gainBuf_.data(), n);
    const float* g = gainBuf_.data();
    const float* srr = sr ? sr : sl;
    for (int i = 0; i < n; ++i)
    {
        l[i] += sl[i] * g[i];
        r[i] += srr[i] * g[i];
    }
}

void RoutingEngine::process(const RoutingInputs& in, RoutingOutputs& out, int n) noexcept
{
    updateTargets();
    for (int b = 0; b < kBusCount; ++b)
    {
        std::memset(out.left[static_cast<size_t>(b)], 0, sizeof(float) * static_cast<size_t>(n));
        std::memset(out.right[static_cast<size_t>(b)], 0, sizeof(float) * static_cast<size_t>(n));
    }

    // 1. Channel output stage: pre (muted) and post (faded + muted), pan gains.
    for (int ch = 0; ch < kRoutingChannels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        float* pre = row(pre_, ch, maxBlock_);
        float* post = row(post_, ch, maxBlock_);
        float* pl = row(panLBuf_, ch, maxBlock_);
        float* pr = row(panRBuf_, ch, maxBlock_);
        pan_[c].left.fill(pl, n);
        pan_[c].right.fill(pr, n);
        const float* x = in.channel[c];
        if (!x)
        {
            std::memset(pre, 0, sizeof(float) * static_cast<size_t>(n));
            std::memset(post, 0, sizeof(float) * static_cast<size_t>(n));
            fader_[c].fill(gainBuf_.data(), n);
            mute_[c].fill(gainBuf_.data(), n);
            continue;
        }
        mute_[c].fill(tmpL_.data(), n);
        fader_[c].fill(tmpR_.data(), n);
        for (int i = 0; i < n; ++i)
        {
            pre[i] = x[i] * tmpL_[static_cast<size_t>(i)];
            post[i] = pre[i] * tmpR_[static_cast<size_t>(i)];
        }
    }

    float* mainL = out.left[idx(BusId::Main)];
    float* mainR = out.right[idx(BusId::Main)];
    float* cleanL = out.left[idx(BusId::Clean)];
    float* cleanR = out.right[idx(BusId::Clean)];
    float* musL = out.left[idx(BusId::MusicOut)];
    float* musR = out.right[idx(BusId::MusicOut)];

    // Centre pan for mono non-channel sources.
    float cl, cr;
    panLaw(0.0f, cl, cr);
    std::fill(tmpL_.begin(), tmpL_.begin() + n, cl);
    std::fill(tmpR_.begin(), tmpR_.begin() + n, cr);

    // 2. Main, Clean, MusicOut.
    for (int ch = 0; ch < kRoutingChannels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        const float* post = in.channel[c] ? row(post_, ch, maxBlock_) : nullptr;
        addMono(mainL, mainR, post, matrix_[c][idx(BusId::Main)], row(panLBuf_, ch, maxBlock_), row(panRBuf_, ch, maxBlock_), n);
        addMono(cleanL, cleanR, post, matrix_[c][idx(BusId::Clean)], row(panLBuf_, ch, maxBlock_), row(panRBuf_, ch, maxBlock_), n);
    }
    const auto music = static_cast<size_t>(idx(SourceId::Music));
    const auto carts = static_cast<size_t>(idx(SourceId::Carts));
    const auto remote = static_cast<size_t>(idx(SourceId::Remote));
    addStereo(mainL, mainR, in.musicL, in.musicR, matrix_[music][idx(BusId::Main)], n);
    addStereo(mainL, mainR, in.cartsL, in.cartsR, matrix_[carts][idx(BusId::Main)], n);
    addStereo(musL, musR, in.musicL, in.musicR, matrix_[music][idx(BusId::MusicOut)], n);
    addStereo(musL, musR, in.cartsL, in.cartsR, matrix_[carts][idx(BusId::MusicOut)], n);
    addMono(mainL, mainR, in.remote, matrix_[remote][idx(BusId::Main)], tmpL_.data(), tmpR_.data(), n);
    addMono(cleanL, cleanR, in.remote, matrix_[remote][idx(BusId::Clean)], tmpL_.data(), tmpR_.data(), n);
    // Talkback on the program buses only when explicitly enabled (the lock is in updateTargets).
    addMono(mainL, mainR, in.talkback, talkbackProgram_, tmpL_.data(), tmpR_.data(), n);
    if (in.talkback && talkbackProgram_.current() > 0.0f)
        for (int i = 0; i < n; ++i)
        {
            cleanL[i] += in.talkback[i] * talkbackProgram_.current() * cl;
            cleanR[i] += in.talkback[i] * talkbackProgram_.current() * cr;
        }

    // 3. Master fader / mute on the program buses.
    masterGain_.fill(gainBuf_.data(), n);
    for (int i = 0; i < n; ++i)
    {
        const float g = gainBuf_[static_cast<size_t>(i)];
        mainL[i] *= g;
        mainR[i] *= g;
        cleanL[i] *= g;
        cleanR[i] *= g;
    }

    // 4. Headphone buses.
    for (int hp = 0; hp < kRoutingChannels; ++hp)
    {
        const auto h = static_cast<size_t>(hp);
        const int bus = hpBus(hp);
        float* l = out.left[static_cast<size_t>(bus)];
        float* r = out.right[static_cast<size_t>(bus)];
        const bool preFader = params_.headphones[h].preFader.load(std::memory_order_relaxed);
        const bool wantMatrix = hpMainness_[h].ramping() || hpMainness_[h].current() < 1.0f;
        if (wantMatrix)
        {
            for (int ch = 0; ch < kRoutingChannels; ++ch)
            {
                const auto c = static_cast<size_t>(ch);
                const float* src = in.channel[c] ? row(preFader ? pre_ : post_, ch, maxBlock_) : nullptr;
                addMono(l, r, src, matrix_[c][static_cast<size_t>(bus)], row(panLBuf_, ch, maxBlock_), row(panRBuf_, ch, maxBlock_), n);
            }
            addStereo(l, r, in.musicL, in.musicR, matrix_[music][static_cast<size_t>(bus)], n);
            addStereo(l, r, in.cartsL, in.cartsR, matrix_[carts][static_cast<size_t>(bus)], n);
            addMono(l, r, in.remote, matrix_[remote][static_cast<size_t>(bus)], tmpL_.data(), tmpR_.data(), n);
        }
        else
        {
            // Keep matrix ramps advancing even while unused.
            for (size_t s = 0; s < kSourceCount; ++s)
                if (matrix_[s][static_cast<size_t>(bus)].ramping()) matrix_[s][static_cast<size_t>(bus)].fill(gainBuf_.data(), n);
        }
        // Crossfade matrix mix ↔ Main (mode switch), dim for talkback, add talkback, volume.
        hpMainness_[h].fill(gainBuf_.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float m = gainBuf_[static_cast<size_t>(i)];
            l[i] = l[i] * (1.0f - m) + mainL[i] * m;
            r[i] = r[i] * (1.0f - m) + mainR[i] * m;
        }
        hpTalkbackDim_[h].fill(gainBuf_.data(), n);
        for (int i = 0; i < n; ++i)
        {
            l[i] *= gainBuf_[static_cast<size_t>(i)];
            r[i] *= gainBuf_[static_cast<size_t>(i)];
        }
        addMono(l, r, in.talkback, talkbackToHp_[h], tmpL_.data(), tmpR_.data(), n);
        hpVolume_[h].fill(gainBuf_.data(), n);
        for (int i = 0; i < n; ++i)
        {
            l[i] *= gainBuf_[static_cast<size_t>(i)];
            r[i] *= gainBuf_[static_cast<size_t>(i)];
        }
    }

    // 5. PFL: pre-fader (post-DSP, pre-mute) sum, mono.
    float* pflL = out.left[idx(BusId::Pfl)];
    float* pflR = out.right[idx(BusId::Pfl)];
    for (int ch = 0; ch < kRoutingChannels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        const float* x = in.channel[c];
        if (!x || (!pfl_[c].ramping() && pfl_[c].current() == 0.0f))
        {
            if (pfl_[c].ramping()) pfl_[c].fill(gainBuf_.data(), n);
            continue;
        }
        pfl_[c].fill(gainBuf_.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float v = x[i] * gainBuf_[static_cast<size_t>(i)];
            pflL[i] += v;
            pflR[i] += v;
        }
    }

    // 6. Solo-in-place mix (monitor only): post-fader soloed channels, panned, × master.
    const bool soloNeeded = soloBlend_.ramping() || soloBlend_.current() > 0.0f;
    if (soloNeeded)
    {
        std::memset(soloL_.data(), 0, sizeof(float) * static_cast<size_t>(n));
        std::memset(soloR_.data(), 0, sizeof(float) * static_cast<size_t>(n));
        for (int ch = 0; ch < kRoutingChannels; ++ch)
        {
            const auto c = static_cast<size_t>(ch);
            const float* post = in.channel[c] ? row(post_, ch, maxBlock_) : nullptr;
            addMono(soloL_.data(), soloR_.data(), post, solo_[c], row(panLBuf_, ch, maxBlock_), row(panRBuf_, ch, maxBlock_), n);
        }
        const float mg = masterGain_.current(); // the ramp was advanced for this block in step 3
        for (int i = 0; i < n; ++i)
        {
            soloL_[static_cast<size_t>(i)] *= mg;
            soloR_[static_cast<size_t>(i)] *= mg;
        }
    }
    else
    {
        for (auto& s : solo_)
            if (s.ramping()) s.fill(gainBuf_.data(), n);
    }

    // 7. Monitor.
    float* monL = out.left[idx(BusId::Monitor)];
    float* monR = out.right[idx(BusId::Monitor)];
    const float* srcL = mainL;
    const float* srcR = mainR;
    switch (monitorSource_)
    {
        case MonitorSource::Main: break;
        case MonitorSource::Pfl: srcL = pflL; srcR = pflR; break;
        case MonitorSource::Clean: srcL = cleanL; srcR = cleanR; break;
        default:
        {
            const int hp = static_cast<int>(monitorSource_) - static_cast<int>(MonitorSource::Hp1);
            srcL = out.left[static_cast<size_t>(hpBus(hp))];
            srcR = out.right[static_cast<size_t>(hpBus(hp))];
        }
    }
    std::memcpy(monL, srcL, sizeof(float) * static_cast<size_t>(n));
    std::memcpy(monR, srcR, sizeof(float) * static_cast<size_t>(n));
    if (soloNeeded)
    {
        soloBlend_.fill(gainBuf_.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float b = gainBuf_[static_cast<size_t>(i)];
            if (monitorSource_ == MonitorSource::Main || b > 0.0f)
            {
                monL[i] = monL[i] * (1.0f - b) + soloL_[static_cast<size_t>(i)] * b;
                monR[i] = monR[i] * (1.0f - b) + soloR_[static_cast<size_t>(i)] * b;
            }
        }
    }
    if (params_.monitor.mono.load(std::memory_order_relaxed))
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (monL[i] + monR[i]);
            monL[i] = monR[i] = m;
        }
    monitorGain_.fill(gainBuf_.data(), n);
    for (int i = 0; i < n; ++i)
    {
        monL[i] *= gainBuf_[static_cast<size_t>(i)];
        monR[i] *= gainBuf_[static_cast<size_t>(i)];
    }
}

} // namespace pf8
