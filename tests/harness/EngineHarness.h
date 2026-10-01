#pragma once
// Offline engine harness (TESTING.md §2).
//
// Fake devices with their own clock offsets (ppm), callback periods and jitter are scheduled on a
// simulated timeline and call the *real* bridge device sides; the master fake device drives the
// *real* AudioEngine tick exactly as a WASAPI master would. Nothing here touches real devices.
#include <array>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/Bridges.h"
#include "engine/EngineGraph.h"

namespace pf8test {

struct FakeDeviceSpec
{
    std::string name;
    int rate = 48000;
    int period = 480;
    int channels = 1;
    double ppm = 0.0;      // device clock error relative to true time
    double jitterUs = 0.0; // uniform callback jitter (±)
    bool master = false;
    // Extra slowly-varying error (bounded random walk, ± µs) on the time the device's callbacks are
    // observed at — models the multi-millisecond wobble seen on real WASAPI devices.
    double timestampNoiseUs = 0.0;
    // Input devices: sample for device channel `ch` at true time `t` (seconds).
    std::function<float(int ch, double t)> signal;
};

class EngineHarness : public pf8::EngineTap
{
public:
    EngineHarness(int engineRate, int engineBlock);
    ~EngineHarness() override;

    int addInput(const FakeDeviceSpec& spec);
    int addOutput(const FakeDeviceSpec& spec);
    void route(int channel, int input, int inputChannel, int output, int pair = 0);
    void routeTalkback(int input, int inputChannel) { talkbackRoute_ = pf8::ChannelRoute{input, inputChannel, -1, 0}; }
    void commitGraph();

    // Simulated hot-plug: the device stops calling back and its bridge leaves the graph; on
    // reconnect a fresh bridge is created (as the controller does) and the route is restored.
    void scheduleDisconnect(int input, double atSeconds);
    void scheduleReconnect(int input, double atSeconds);

    // Keep audio only from `fromSeconds` on (bounded memory for long runs).
    void captureFrom(double fromSeconds) { captureFrom_ = fromSeconds; }

    void run(double seconds);

    pf8::AudioEngine& engine() noexcept { return engine_; }
    const std::vector<float>& outputCapture(int output) const { return outputs_[static_cast<size_t>(output)].captured; }
    const std::vector<float>& channelCapture(int channel) const { return channelCapture_[static_cast<size_t>(channel)]; }
    // Engine-sample positions (sub-sample) of the largest |x| in each 1 s window, per channel.
    const std::vector<double>& clickPositions(int channel) const { return clicks_[static_cast<size_t>(channel)]; }
    pf8::BridgeStats inputStats(int i) const { return inputs_[static_cast<size_t>(i)].in->read(); }
    pf8::BridgeStats outputStats(int i) const { return outputs_[static_cast<size_t>(i)].out->read(); }
    uint64_t realtimeAllocations() const noexcept { return rtAllocations_; }
    uint64_t engineFrames() const noexcept { return engineFrames_; }

    void onChannelBlock(const float* const* channels, int numChannels, int frames) noexcept override;

private:
    struct Device
    {
        FakeDeviceSpec spec;
        bool isInput = true;
        std::shared_ptr<pf8::InputBridge> in;
        std::shared_ptr<pf8::OutputBridge> out;
        uint64_t callbacks = 0;
        uint64_t frames = 0;
        double nextTime = 0.0;
        bool connected = true;
        std::vector<float> buffer;
        std::vector<float> captured; // outputs: device channel 0
        std::mt19937 rng;            // per-device jitter, so one device's events never perturb another
        double tsError = 0.0;        // current timestamp error (ns), bounded random walk
    };
    struct Event { double at; int input; bool connect; };

    pf8::BridgeConfig configFor(const FakeDeviceSpec& s) const;
    double callbackTime(Device& d);
    void rebuildGraph();
    void fire(Device& d);
    static int64_t clockFn(void* ctx) noexcept;

    int engineRate_, engineBlock_;
    pf8::AudioEngine engine_;
    std::vector<Device> inputs_;
    std::vector<Device> outputs_;
    std::array<pf8::ChannelRoute, pf8::kNumChannels> routes_{};
    pf8::ChannelRoute talkbackRoute_{};
    std::vector<Event> events_;
    std::array<std::vector<float>, pf8::kNumChannels> channelCapture_;
    std::array<std::vector<double>, pf8::kNumChannels> clicks_;
    std::array<double, pf8::kNumChannels> windowMax_{};
    std::array<double, pf8::kNumChannels> windowPos_{};
    std::array<float, pf8::kNumChannels> prev1_{}, prev2_{};
    double nowSec_ = 0.0;
    double captureFrom_ = 0.0;
    uint64_t generation_ = 0;
    uint64_t engineFrames_ = 0;
    uint64_t rtAllocations_ = 0;
    bool hasMaster_ = false;
    int masterBurst_ = 0;
    double nextInternalTick_ = 0.0;
};

} // namespace pf8test
