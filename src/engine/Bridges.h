#pragma once
// Bridges between device clocks and the engine clock (AUDIO_ENGINE.md §3).
//
// Each bridge has a *device side* (called on the device's stream thread, or by the test harness)
// and an *engine side* (called from the tick). The two sides share only an SPSC ring and atomics.
//
// Non-master bridges run a VarResampler steered by a DriftController. The fill fed to the
// controller is linearised: frames the device has produced (input) or consumed (output) since its
// last callback are accounted for using the elapsed time, so the device's staircase delivery does
// not alias into the loop.
//
// A master bridge runs at the fixed nominal ratio (exact passthrough when rates match) and drives
// the engine: an output master calls TickClient::tick until it holds enough frames for the device
// request; an input master ticks whenever a full engine block can be produced.
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/SeqLock.h"
#include "core/SpscRing.h"
#include "engine/DriftController.h"
#include "engine/StreamTypes.h"
#include "engine/TimeDll.h"
#include "engine/VarResampler.h"

namespace pf8 {

struct BridgeConfig
{
    int deviceChannels = 2;
    int deviceRate = 48000;
    int devicePeriod = 480;  // frames per device callback (device rate)
    int engineRate = 48000;
    int engineBlock = 128;
    // Engine frames processed back-to-back per master callback (master pull model). Non-master
    // bridges must buffer a whole burst. 0 = engineBlock (internal clock).
    int engineBurst = 0;
    // Render only: frames the device keeps in its own (WASAPI) buffer on average; included in the
    // regulated latency because hardware-position measurement counts them.
    int deviceBufferFrames = 0;
    bool master = false;
};

struct BridgeStats
{
    uint64_t underruns = 0;
    uint64_t overruns = 0;
    uint64_t droppedFrames = 0;
    double ppm = 0.0;
    double fill = 0.0;   // averaged, device frames
    double target = 0.0; // device frames
    SyncStatus status = SyncStatus::Offline;
    double timestampAgeMs = 0.0; // input: engine time minus the end of the last packet (hardware time)
    uint64_t deviceFrames = 0;   // frames delivered by (input) / taken by (output) the device
    uint64_t recentres = 0;      // one-time start-up re-centring events
    // Diagnostics (instantaneous values of the last update).
    double measuredNow = 0.0;    // linearised fill used by the loop
    double ringNow = 0.0;        // raw ring frames
    double clockOffsetUs = 0.0;  // master: DLL time minus callback time
};

// Target ring fill in device frames: covers one device period plus bursts of engine blocks,
// with 2 ms of scheduling jitter margin.
int bridgeTargetFill(const BridgeConfig& c) noexcept;

class BridgeCounters
{
public:
    BridgeStats read() const noexcept;
protected:
    std::atomic<uint64_t> underruns_{0}, overruns_{0}, dropped_{0};
    std::atomic<double> ppm_{0.0}, fill_{0.0};
    std::atomic<SyncStatus> status_{SyncStatus::Priming};
    std::atomic<double> tsAgeMs_{0.0};
    std::atomic<uint64_t> deviceFrames_{0};
    std::atomic<uint64_t> recentres_{0};
    std::atomic<double> measuredNow_{0.0}, ringNow_{0.0}, clockOffsetUs_{0.0};
    // Start-up re-centring: WASAPI streams deliver unevenly for their first moments, so the fill
    // measured at priming can differ from the steady state by most of a device period. Once the
    // averaged fill has settled (1.5 s after start) a single correction re-centres the bridge.
    int64_t settleFramesLeft_ = 0; // engine side
    static constexpr double kSettleSeconds = 1.5;
    double target_ = 0.0;
};

class InputBridge : public BridgeCounters
{
public:
    explicit InputBridge(const BridgeConfig& config);

    // Device side. `callbackNs`: time of this callback (monotonic). It is DLL-filtered into the
    // device's own smooth timeline; raw WASAPI packet timestamps are deliberately not used.
    void deviceWrite(const float* interleaved, int frames, int64_t callbackNs) noexcept;
    // Master input: tick the engine while a full block is available (device thread).
    // `nowNs`: callback time; it is DLL-filtered into the engine's time base.
    void driveEngine(TickClient& engine, int64_t nowNs = 0) noexcept;

    // Engine side: produce `frames` engine-rate frames, one buffer per device channel.
    void engineRead(float* const* perChannel, int frames, int64_t nowNs) noexcept;

    const BridgeConfig& config() const noexcept { return cfg_; }
    size_t readableDeviceFrames() const noexcept { return ring_.size() / static_cast<size_t>(cfg_.deviceChannels); }

private:
    void outputSilence(float* const* perChannel, int frames) noexcept;

    BridgeConfig cfg_;
    SpscRing<float> ring_;
    VarResampler rs_;
    DriftController dc_;
    std::vector<float> staging_;   // popped device frames (interleaved)
    std::vector<float> resampled_; // engine-rate frames (interleaved)
    std::atomic<int64_t> lastEndNs_{0}; // hardware time just after the last delivered frame
    bool running_ = false; // engine side
    int fadeRemaining_ = 0;
    int fadeLength_ = 0;
    int silenceFrames_ = 0; // re-centre deficit: frames of silence still to emit
    TimeDll dll_;           // device timeline (and the engine's time base when master)
};

class OutputBridge : public BridgeCounters
{
public:
    // pairs: number of stereo pairs the engine writes (channel N of the device = pair N/2).
    explicit OutputBridge(const BridgeConfig& config, int pairs = 1);

    // Engine side: write every pair for this block, then commit once.
    void engineWritePair(int pair, const float* left, const float* right, int frames) noexcept;
    void engineCommit(int frames, int64_t nowNs) noexcept;

    // Device side. For a master bridge, `driver` is ticked until enough frames exist. `nowNs` is the
    // callback time; it is DLL-filtered (the engine's "now" for a master, the device's timeline
    // otherwise).
    void deviceRead(float* interleaved, int frames, int64_t nowNs, TickClient* driver = nullptr) noexcept;

    const BridgeConfig& config() const noexcept { return cfg_; }
    int pairs() const noexcept { return pairs_; }
    size_t readableDeviceFrames() const noexcept { return ring_.size() / static_cast<size_t>(cfg_.deviceChannels); }

private:
    BridgeConfig cfg_;
    int pairs_;
    SpscRing<float> ring_;
    VarResampler rs_;
    DriftController dc_;
    std::vector<float> pairStaging_; // engine-rate interleaved, 2 × pairs channels
    std::vector<float> resampled_;   // device-rate interleaved, 2 × pairs channels
    std::vector<float> deviceFrames_;// device-rate interleaved, deviceChannels
    int64_t skipFrames_ = 0;         // re-centre excess still to drop (engine side)
    TimeDll dll_;                    // device timeline (and the engine's time base when master)
    // Consumption position published by the device side: `ringRead` frames had been taken from
    // the ring when the packet starting at `startNs` began playing.
    struct ReadPos { uint64_t ringRead; int64_t startNs; };
    SeqLockSnapshot<ReadPos> readPos_;
    uint64_t totalRead_ = 0;    // device side: frames popped from the ring
    uint64_t totalWritten_ = 0; // engine side: frames pushed into the ring
    std::atomic<bool> devicePriming_{true};
    int fadeRemaining_ = 0; // device side
    int fadeLength_ = 0;
};

} // namespace pf8
