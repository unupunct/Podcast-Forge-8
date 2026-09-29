#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <objbase.h>

#include "devices/DeviceInfo.h"
#include "devices/WinEndpointEnumerator.h"
#include "engine/AudioEngine.h"

using namespace pf8;

TEST_CASE("Live: engine runs on a real render endpoint by endpoint id", "[live][engine]")
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string renderId, name;
    for (const auto& e : enumerateEndpoints(false))
        if (e.state == DeviceState::Active && e.flow == Flow::Render)
        {
            renderId = e.endpointId;
            name = e.friendlyName;
            break;
        }
    if (renderId.empty())
    {
        CoUninitialize();
        SKIP("no active render endpoint");
    }

    AudioEngine engine(48000, 128);
    StreamConfig cfg;
    cfg.endpointId = renderId;
    cfg.flow = Flow::Render;
    cfg.mode = StreamMode::Shared;
    std::string error;
    REQUIRE(engine.start(cfg, name, error));
    Sleep(1000);
    auto s = engine.status();
    engine.stop();
    CoUninitialize();

    CHECK_FALSE(s.internalClock);
    CHECK(s.masterId == renderId);
    CHECK(s.masterStatus == StreamStatus::Running);
    CHECK(s.sampleRate > 0);
    CHECK(s.grantedPeriod > 0);
    CHECK(s.ticks >= 50);
    CHECK(s.load < 0.5);
}

TEST_CASE("Live: unknown endpoint id falls back to the internal clock", "[live][engine]")
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AudioEngine engine(48000, 480);
    StreamConfig cfg;
    cfg.endpointId = "{0.0.0.00000000}.{00000000-0000-0000-0000-000000000000}";
    std::string error;
    REQUIRE(engine.start(cfg, "missing", error));
    Sleep(300);
    auto s = engine.status();
    engine.stop();
    CoUninitialize();
    CHECK(s.internalClock);
    CHECK(s.ticks >= 20);
    CHECK_FALSE(error.empty());
}
