#pragma once
// The routing matrix (ROUTING.md). Pure DSP: no devices, no threads. process() is real-time safe.
//
// Per block:
//   channel pre (post-DSP) ─┬─ × fader × mute/cough ─→ post ─→ Main / Clean (panned)
//                           ├─ (pre- or post-fader) sends ─→ HP 1–8 (Personal/Custom), panned
//                           └─ PFL (pre-fader, mono)
//   music / carts ─→ Main, MusicOut, HP ;  remote ─→ Main, Clean, HP
//   talkback ─→ target HP buses while active; Main/Clean only if talkbackToProgram
//   Main × master fader/mute ;  HP (Main mode) = Main ;  HP × volume/mute
//   Monitor = source (Main with solo-in-place | PFL | Clean | HP n) × mono/dim/volume/mute
#include <array>
#include <vector>

#include "routing/Ramp.h"
#include "routing/RoutingParams.h"
#include "routing/RoutingTypes.h"

namespace pf8 {

struct RoutingInputs
{
    std::array<const float*, kRoutingChannels> channel{}; // mono, post-DSP; nullptr = silent
    const float* musicL = nullptr;
    const float* musicR = nullptr;
    const float* cartsL = nullptr;
    const float* cartsR = nullptr;
    const float* talkback = nullptr; // mono
    const float* remote = nullptr;   // mono
};

struct RoutingOutputs
{
    std::array<float*, kBusCount> left{};
    std::array<float*, kBusCount> right{};
};

class RoutingEngine
{
public:
    void prepare(double sampleRate, int maxBlock);
    void process(const RoutingInputs& in, RoutingOutputs& out, int frames) noexcept;

    RoutingParams& params() noexcept { return params_; }
    const RoutingParams& params() const noexcept { return params_; }

    // Tick-side state for meters / UI.
    bool anySolo() const noexcept { return anySolo_; }
    bool anyPfl() const noexcept { return anyPfl_; }
    MonitorSource effectiveMonitorSource() const noexcept { return monitorSource_; }

private:
    struct PanGains { Ramp left, right; };

    void updateTargets() noexcept;
    static void panLaw(float pan, float& l, float& r) noexcept;
    void addMono(float* l, float* r, const float* src, Ramp& gain, const float* panL, const float* panR, int n) noexcept;
    void addStereo(float* l, float* r, const float* sl, const float* sr, Ramp& gain, int n) noexcept;

    RoutingParams params_;
    int maxBlock_ = 0;

    std::array<Ramp, kRoutingChannels> fader_, mute_, pfl_, solo_;
    std::array<PanGains, kRoutingChannels> pan_;
    std::array<std::array<Ramp, kBusCount>, kSourceCount> matrix_;
    std::array<Ramp, kRoutingChannels> hpVolume_, hpTalkbackDim_, hpMainness_;
    Ramp masterGain_, talkbackGain_, talkbackProgram_;
    std::array<Ramp, kRoutingChannels> talkbackToHp_;
    Ramp monitorGain_, soloBlend_;

    // Scratch (allocated in prepare).
    std::vector<float> post_, pre_, gainBuf_, panLBuf_, panRBuf_, tmpL_, tmpR_;
    std::vector<float> soloL_, soloR_;
    bool anySolo_ = false, anyPfl_ = false;
    MonitorSource monitorSource_ = MonitorSource::Main;
    std::array<HpMode, kRoutingChannels> hpMode_{};
};

} // namespace pf8
