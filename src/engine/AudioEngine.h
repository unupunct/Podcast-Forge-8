#pragma once
// The engine tick. Device-agnostic: live WASAPI streams and the offline test harness drive it
// through the same bridges.
//
// Threading: tick() runs on whichever thread drives the clock (master device, internal clock, or
// harness). It is guarded so a second concurrent caller skips instead of re-entering. Everything
// else is called from the control thread.
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "core/MpscQueue.h"
#include "core/SeqLock.h"
#include "core/SpscRing.h"
#include "engine/EngineGraph.h"
#include "engine/InternalClock.h"
#include "engine/StreamTypes.h"
#include "dsp/ChannelStrip.h"
#include "dsp/DspParams.h"
#include "dsp/Dynamics.h"
#include "dsp/Reverb.h"
#include "dsp/Ducker.h"
#include "media/MusicPlayer.h"
#include "media/Soundboard.h"
#include "record/RecordTap.h"
#include "routing/RoutingEngine.h"

namespace pf8 {

struct EngineMeters
{
    std::array<float, kNumChannels> peak{};  // block peak, linear
    std::array<float, kNumChannels> rms{};   // block RMS, linear
    double load = 0.0;                       // tick time / block time (EWMA)
    double loadPeak = 0.0;                   // max over the last second
    uint64_t ticks = 0;
    uint64_t skippedTicks = 0;               // concurrent tick attempts rejected by the guard
    uint64_t graphGeneration = 0;
    std::array<std::array<float, 2>, kBusCount> busPeak{}; // per bus, L/R block peak
    bool anySolo = false, anyPfl = false;
    std::array<dsp::StripMeters, kNumChannels> strip{};     // gate / comp / de-ess / limiter activity
    float masterLimiterGrDb = 0.0f;
    std::array<float, kNumChannels> hpProtectGrDb{}; // ≤ 0 when the protection limiter acts
    float musicDuckDb = 0.0f;                         // ≤ 0 while ducking
    float monitorProtectGrDb = 0.0f;
};

// Receives each block's channel buffers (post-input, pre-processing) on the tick thread.
// Used by the test harness now and by the recorder tap later. Must be real-time safe.
class EngineTap
{
public:
    virtual ~EngineTap() = default;
    virtual void onChannelBlock(const float* const* channels, int numChannels, int frames) noexcept = 0;
};

class AudioEngine : public TickClient
{
public:
    using ClockFn = int64_t (*)(void* ctx) noexcept;

    AudioEngine(int sampleRate, int blockFrames);
    ~AudioEngine() override;

    int sampleRate() const noexcept { return sampleRate_; }
    int blockFrames() const noexcept { return blockFrames_; }

    // Test hook: the time source bridges use for fill linearisation (default: QPC).
    void setClock(ClockFn fn, void* ctx) noexcept { clockFn_ = fn; clockCtx_ = ctx; }

    // Control thread: publish a new graph; it takes effect at the next block boundary.
    void setGraph(std::unique_ptr<EngineGraph> graph);
    // Control thread: destroy graphs the engine has stopped using. Returns how many were freed.
    int collectGarbage();
    // Control thread: the generation the tick is currently using.
    uint64_t activeGeneration() const noexcept { return activeGeneration_.load(); }

    void startInternalClock();
    void stopInternalClock();
    bool internalClockRunning() const noexcept { return internal_ && internal_->running(); }

    EngineMeters meters() const noexcept { return meterSnapshot_.read(); }
    void setTap(EngineTap* tap) noexcept { tap_.store(tap, std::memory_order_release); }

    // Parameters (UI/control threads write, the tick reads).
    RoutingParams& routing() noexcept { return routing_.params(); }
    ChannelDspParams& dsp(int channel) noexcept { return dsp_[static_cast<size_t>(channel)]; }
    MasterDspParams& masterDsp() noexcept { return masterDsp_; }
    std::atomic<bool>& recordArm(int channel) noexcept { return recordArm_[static_cast<size_t>(channel)]; }

    // Mic wizard: the raw input (before trim and DSP) of one channel is copied into a ring the UI
    // thread drains. -1 stops it.
    void setAnalysisChannel(int channel) noexcept { analysisChannel_.store(channel, std::memory_order_release); }
    size_t readAnalysis(float* dst, size_t max) noexcept { return analysisRing_.pop(dst, max); }
    int dspLatency() const noexcept { return strips_[0].latency(); }

    // Recording hand-off (the recorder configures and activates it).
    RecordTap& recordTap() noexcept { return recordTap_; }
    Soundboard& soundboard() noexcept { return soundboard_; }
    MusicPlayer& music() noexcept { return *music_; }
    dsp::DuckerParams& ducker() noexcept { return duckerParams_; }

    void tick(int numFrames) noexcept override;
    void setTickTimeNs(int64_t t) noexcept override { tickTimeNs_ = t; }

private:
    void applyPendingGraph() noexcept;
    void processBlock(int frames, int64_t nowNs) noexcept;

    int sampleRate_;
    int blockFrames_;
    ClockFn clockFn_;
    void* clockCtx_ = nullptr;

    std::atomic_flag ticking_ = ATOMIC_FLAG_INIT;
    MpscQueue<EngineGraph*> pending_{64};
    SpscRing<EngineGraph*> retired_{128};
    EngineGraph* graph_ = nullptr; // tick thread
    std::atomic<uint64_t> activeGeneration_{0};
    std::vector<std::unique_ptr<EngineGraph>> owned_; // control thread: every graph not yet freed

    // Pre-allocated tick buffers.
    std::vector<float> bridgeBuffers_; // [bridge][deviceChannel][kMaxBlock]
    std::vector<float*> bridgePtrs_;   // [bridge][deviceChannel]
    std::vector<float> channelBuffers_; // [channel][kMaxBlock]

    std::unique_ptr<InternalClock> internal_;
    SeqLockSnapshot<EngineMeters> meterSnapshot_;
    EngineMeters meters_{}; // tick thread
    double peakAccum_ = 0.0;
    int64_t peakWindowFrames_ = 0;
    std::atomic<uint64_t> skipped_{0};
    std::atomic<EngineTap*> tap_{nullptr};
    int64_t tickTimeNs_ = 0; // set by a master bridge right before tick(); 0 = use the clock
    RoutingEngine routing_;
    std::array<ChannelDspParams, kNumChannels> dsp_;
    MasterDspParams masterDsp_;
    std::array<std::atomic<bool>, kNumChannels> recordArm_{};
    std::array<dsp::ChannelStrip, kNumChannels> strips_;
    std::atomic<int> analysisChannel_{-1};
    SpscRing<float> analysisRing_{48000 * 12};
    dsp::Reverb reverb_;
    std::vector<float> reverbIn_, fxL_, fxR_;
    dsp::Limiter mainLimiter_, cleanLimiter_;
    std::array<dsp::Limiter, kNumChannels> hpLimiters_; // hearing protection per headphone feed
    dsp::Limiter monitorLimiter_;
    RecordTap recordTap_;
    Soundboard soundboard_{48000};
    std::vector<float> cartsL_, cartsR_;
    std::unique_ptr<MusicPlayer> music_;
    dsp::Ducker ducker_;
    dsp::DuckerParams duckerParams_;
    std::vector<float> musicL_, musicR_, duckGain_, voice_, recordMusic_;
    int prevFrames_ = 0;
    // Isolated tracks are delayed by the master limiter's look-ahead so they align with Main.
    std::vector<float> recordDelay_;  // [channel][L]
    int recordDelayPos_ = 0;
    std::vector<float> recordCh_;     // [channel][kMaxBlock]
    std::vector<float> recordMain_;   // interleaved stereo
    std::vector<float> busBuffers_; // [bus][L/R][kMaxBlock]
    RoutingInputs routingIn_{};
    RoutingOutputs routingOut_{};
    std::array<const float*, kNumChannels> channelPtrs_{};
};

} // namespace pf8
