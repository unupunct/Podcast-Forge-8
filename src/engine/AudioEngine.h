#pragma once
// Owns the master clock source and runs the tick. Stage 1: the tick produces silence and measures
// load; later stages add sources, DSP, routing, recording.
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "core/SeqLock.h"
#include "engine/InternalClock.h"
#include "engine/StreamTypes.h"
#include "engine/WasapiStream.h"

namespace pf8 {

class AudioEngine : public TickClient
{
public:
    struct Status
    {
        std::string backend;       // "WASAPI"
        std::string masterId;      // endpoint id, empty with the internal clock
        std::string masterName;
        StreamMode mode = StreamMode::Shared;
        std::string sampleFormat;
        int sampleRate = 0;
        int requestedFrames = 0;
        int grantedPeriod = 0;     // frames per device period
        bool internalClock = false;
        bool running = false;
        double load = 0.0;         // tick time / block time, EWMA
        double loadPeak = 0.0;     // max over the last second
        uint64_t ticks = 0;
        uint64_t glitches = 0;
        StreamStatus masterStatus = StreamStatus::Closed;
    };

    explicit AudioEngine(int sampleRate = 48000, int blockFrames = 128);
    ~AudioEngine() override;

    // Starts the engine driven by `master` (a render or capture endpoint), or by the internal
    // clock when nullopt or when the master fails to open. Returns false only if nothing runs.
    bool start(const std::optional<StreamConfig>& master, const std::string& masterName, std::string& error);
    void stop();

    Status status() const;
    void tick(int numFrames) noexcept override;

private:
    class MasterCallback;
    struct RtStats
    {
        double load = 0.0;
        double loadPeak = 0.0;
        uint64_t ticks = 0;
    };

    void renderInto(float* interleaved, int frames, int channels) noexcept;

    int sampleRate_;
    int blockFrames_;
    std::unique_ptr<MasterCallback> masterCallback_;
    std::unique_ptr<WasapiStream> master_;
    std::unique_ptr<InternalClock> internal_;

    mutable std::mutex statusMutex_; // guards config strings (never taken on the tick)
    Status config_;

    SeqLockSnapshot<RtStats> rtSnapshot_;
    RtStats rt_; // tick-thread only
    double peakWindowFrames_ = 0.0;
    double peakAccum_ = 0.0;
    int64_t qpcFrequency_ = 1;
};

} // namespace pf8
