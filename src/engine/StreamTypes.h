#pragma once
#include <atomic>
#include <cstdint>
#include <string>

#include "devices/DeviceInfo.h"

namespace pf8 {

constexpr int kNumChannels = 8;
constexpr int kMaxBlock = 512;

enum class StreamMode : uint8_t { Shared, SharedLowLatency, Exclusive };
enum class StreamStatus : uint8_t { Closed, Running, Failed, Disconnected, InUseExclusive };

struct StreamConfig
{
    std::string endpointId;
    Flow flow = Flow::Render;
    StreamMode mode = StreamMode::Shared;
    int requestedRate = 48000;   // exclusive mode only; shared mode uses the mix rate
    int requestedFrames = 128;   // desired period
    // Shared mode: ask Windows to bypass the endpoint's audio effects (AGC, loudness equalisation,
    // noise suppression, "enhancements") — AUDCLNT_STREAMOPTIONS_RAW. Ignored where unsupported.
    bool raw = true;
};

struct StreamStats
{
    std::atomic<uint64_t> callbacks{0};
    std::atomic<uint64_t> glitches{0};        // discontinuities / timeouts reported by WASAPI
    std::atomic<uint64_t> framesProcessed{0};
    std::atomic<int64_t> lastQpc100ns{0};     // QPC time of the last buffer, 100 ns units
    std::atomic<uint64_t> lastDevicePosition{0};
};

// Receives device buffers on the stream's MMCSS thread. Must be real-time safe.
class StreamCallback
{
public:
    virtual ~StreamCallback() = default;
    // capture: `interleaved` holds the captured frames. render: fill `interleaved` (pre-zeroed).
    virtual void onStreamBlock(float* interleaved, int frames, int channels, int64_t qpc100ns,
                               uint64_t devicePosition) noexcept = 0;
    // Called once from the stream thread when it stops because of an error (not on stop()).
    virtual void onStreamError(StreamStatus) noexcept {}
};

class TickClient
{
public:
    virtual ~TickClient() = default;
    virtual void tick(int numFrames) noexcept = 0;
    // Master bridges call this before each tick with the hardware-clock time (monotonic ns) of the
    // audio that tick produces/consumes, so the engine's notion of "now" follows the master's
    // crystal instead of thread-scheduling time.
    virtual void setTickTimeNs(int64_t) noexcept {}
};

const char* toString(StreamMode m) noexcept;
const char* toString(StreamStatus s) noexcept;

} // namespace pf8
