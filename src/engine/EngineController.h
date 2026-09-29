#pragma once
// The device-control thread (COM MTA). Owns the DeviceRegistry and the AudioEngine, and performs
// every blocking device operation (enumerate, open, close) off both the UI and audio threads.
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "devices/DeviceRegistry.h"
#include "engine/AudioEngine.h"

namespace pf8 {

class EngineController
{
public:
    struct Settings
    {
        int sampleRate = 48000;
        int blockFrames = 128;
        StreamMode mode = StreamMode::Shared;
    };

    explicit EngineController(Settings settings);
    ~EngineController();
    EngineController(const EngineController&) = delete;
    EngineController& operator=(const EngineController&) = delete;

    // Asynchronous; run on the control thread.
    void rescanDevices();
    void startEngineOnDefaultOutput();
    void post(std::function<void()> job);
    void waitIdle(); // blocks the caller until queued jobs have run (tests, shutdown)

    const DeviceRegistry& registry() const noexcept { return registry_; }
    AudioEngine::Status engineStatus() const { return engine_.status(); }
    const Settings& settings() const noexcept { return settings_; }

private:
    void threadMain();

    Settings settings_;
    DeviceRegistry registry_;
    AudioEngine engine_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::deque<std::function<void()>> jobs_;
    bool busy_ = false;
    bool quit_ = false;
};

} // namespace pf8
