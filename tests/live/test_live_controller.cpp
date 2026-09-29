#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cmath>
#include <cstdio>

#include "TestDirs.h"
#include "core/SettingsDb.h"
#include "engine/EngineController.h"

using namespace pf8;

namespace {
std::optional<std::string> findEndpoint(const EngineController& c, Flow flow, const char* nameFragment)
{
    for (const auto& d : c.registry().devices())
        if (d.online() && d.raw.flow == flow && d.raw.friendlyName.find(nameFragment) != std::string::npos)
            return d.raw.endpointId;
    return std::nullopt;
}
double toDb(float v) { return v > 0 ? 20.0 * std::log10(v) : -200.0; }
} // namespace

// Real devices: a USB mic into channel 1, channel 1's headphones on VB-Cable's render side, and
// VB-Cable's capture side into channel 2 - a loop through two independently clocked endpoints.
// Nothing is played on audible outputs.
TEST_CASE("Live: USB mic -> VB-Cable loop through the multi-device engine", "[live][controller]")
{
    pf8test::TempDir dir("live-controller");
    SettingsDb db;
    REQUIRE(db.open(dir.path() / "Settings.db"));
    EngineController c({48000, 128, StreamMode::Shared}, &db);
    c.start();
    c.waitIdle();

    auto mic = findEndpoint(c, Flow::Capture, "BRIO");
    if (!mic) mic = findEndpoint(c, Flow::Capture, "Microphone");
    auto cableIn = findEndpoint(c, Flow::Render, "CABLE Input");
    auto cableOut = findEndpoint(c, Flow::Capture, "CABLE Output");
    if (!mic || !cableIn || !cableOut) SKIP("needs a microphone and VB-Audio Virtual Cable");

    c.assignMic(0, *mic);
    c.assignHeadphones(0, *cableIn);
    c.assignMic(1, *cableOut);
    c.waitIdle();

    // Let the drift loops settle; print the trajectory (real device clocks, real WASAPI timing).
    ControllerStatus s;
    for (int t = 0; t < 8; ++t)
    {
        Sleep(5000);
        s = c.status();
        std::printf("  t=%2ds  ch1 %-10s %8.2f ppm fill %5.0f/%4.0f xr=%llu   ch2 %-10s %8.2f ppm fill %5.0f/%4.0f xr=%llu\n",
                    (t + 1) * 5, toString(s.channels[0].mic.bridge.status), s.channels[0].mic.bridge.ppm,
                    s.channels[0].mic.bridge.fill, s.channels[0].mic.bridge.target,
                    static_cast<unsigned long long>(s.channels[0].mic.bridge.underruns),
                    toString(s.channels[1].mic.bridge.status), s.channels[1].mic.bridge.ppm,
                    s.channels[1].mic.bridge.fill, s.channels[1].mic.bridge.target,
                    static_cast<unsigned long long>(s.channels[1].mic.bridge.underruns));
        if (s.channels[0].mic.bridge.status == SyncStatus::Locked && s.channels[1].mic.bridge.status == SyncStatus::Locked) break;
    }

    auto m = c.meters();
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& v = s.channels[static_cast<size_t>(ch)];
        std::printf("  ch%d mic=%s sync=%s ppm=%.1f fill=%.0f/%.0f xr=%llu peak=%.1f dBFS master=%d tsAge=%.2fms frames=%llu period=%d\n",
                    ch + 1, toString(v.mic.state), toString(v.mic.bridge.status), v.mic.bridge.ppm, v.mic.bridge.fill,
                    v.mic.bridge.target, static_cast<unsigned long long>(v.mic.bridge.underruns),
                    toDb(m.peak[static_cast<size_t>(ch)]), v.mic.master ? 1 : 0, v.mic.bridge.timestampAgeMs,
                    static_cast<unsigned long long>(v.mic.bridge.deviceFrames), v.mic.periodFrames);
    }
    const auto& hp = s.channels[0].headphones;
    std::printf("  hp1 state=%s sync=%s ppm=%.1f master=%d; engine master=%s internal=%d ticks=%llu\n",
                toString(hp.state), toString(hp.bridge.status), hp.bridge.ppm, hp.master ? 1 : 0,
                s.masterName.c_str(), s.internalClock ? 1 : 0, static_cast<unsigned long long>(m.ticks));

    CHECK(s.channels[0].mic.state == EndpointState::Ok);
    CHECK(s.channels[1].mic.state == EndpointState::Ok);
    CHECK(hp.state == EndpointState::Ok);
    CHECK(hp.master);                       // the headphone output drives the tick
    CHECK_FALSE(s.internalClock);
    CHECK(m.ticks > 1000);
    CHECK(s.channels[0].mic.bridge.status == SyncStatus::Locked);
    CHECK(s.channels[1].mic.bridge.status == SyncStatus::Locked);
    // The loop carries audio: channel 2 hears what channel 1 captured (room noise at least).
    CHECK(m.peak[0] > 0.0f);
    CHECK(m.peak[1] > 0.0f);

    // Assignments persisted by identity.
    auto saved = db.get("assignments.v1");
    REQUIRE(saved.has_value());
    auto a = assignmentsFromJson(*saved);
    REQUIRE(a.has_value());
    CHECK(a->ch[0].mic->endpointId == *mic);
    CHECK(a->ch[0].headphones->endpointId == *cableIn);
}
