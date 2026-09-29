#include "engine/EngineController.h"

#include <windows.h>
#include <objbase.h>

#include "core/Log.h"
#include "devices/WinEndpointEnumerator.h"

namespace pf8 {

EngineController::EngineController(Settings settings)
    : settings_(settings), engine_(settings.sampleRate, settings.blockFrames)
{
    thread_ = std::thread([this] { threadMain(); });
}

EngineController::~EngineController()
{
    post([this] { engine_.stop(); });
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void EngineController::post(std::function<void()> job)
{
    {
        std::lock_guard lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void EngineController::waitIdle()
{
    std::unique_lock lock(mutex_);
    idleCv_.wait(lock, [this] { return jobs_.empty() && !busy_; });
}

void EngineController::rescanDevices()
{
    post([this] {
        auto diff = registry_.rebuild(enumerateEndpoints());
        PF8_LOG_INFO("device", "scan added=%zu removed=%zu changed=%zu total=%zu", diff.added.size(),
                     diff.removed.size(), diff.changed.size(), registry_.devices().size());
        for (const auto& id : diff.removed) PF8_LOG_WARN("device", "device.removed id=%s", id.c_str());
    });
}

void EngineController::startEngineOnDefaultOutput()
{
    post([this] {
        std::optional<StreamConfig> master;
        std::string name;
        if (auto id = defaultEndpointId(Flow::Render))
        {
            StreamConfig cfg;
            cfg.endpointId = *id;
            cfg.flow = Flow::Render;
            cfg.mode = settings_.mode;
            cfg.requestedRate = settings_.sampleRate;
            cfg.requestedFrames = settings_.blockFrames;
            master = cfg;
            if (auto info = registry_.find(*id)) name = info->raw.friendlyName;
        }
        std::string error;
        if (!engine_.start(master, name, error))
            PF8_LOG_ERROR("engine", "engine failed to start: %s", error.c_str());
    });
}

void EngineController::threadMain()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;)
    {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
            if (jobs_.empty())
            {
                if (quit_) break;
                continue;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
            busy_ = true;
        }
        try
        {
            job();
        }
        catch (const std::exception& e)
        {
            PF8_LOG_ERROR("control", "job threw: %s", e.what());
        }
        {
            std::lock_guard lock(mutex_);
            busy_ = false;
        }
        idleCv_.notify_all();
    }
    CoUninitialize();
}

} // namespace pf8
