// Hidden diagnostic: records bridge time series on real devices to a CSV ("[.livetrace]").
#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cstdio>
#include <fstream>

#include "TestDirs.h"
#include "engine/EngineController.h"

using namespace pf8;

TEST_CASE("LIVETRACE bridge time series", "[.livetrace]")
{
    EngineController c({48000, 128, StreamMode::Shared}, nullptr);
    c.start();
    c.waitIdle();
    auto find = [&](Flow f, const char* frag) -> std::optional<std::string> {
        for (const auto& d : c.registry().devices())
            if (d.online() && d.raw.flow == f && d.raw.friendlyName.find(frag) != std::string::npos) return d.raw.endpointId;
        return std::nullopt;
    };
    auto mic = find(Flow::Capture, "BRIO");
    auto cableIn = find(Flow::Render, "CABLE Input");
    auto cableOut = find(Flow::Capture, "CABLE Output");
    if (!mic || !cableIn || !cableOut) SKIP("needs devices");
    c.assignMic(0, *mic);
    c.assignHeadphones(0, *cableIn);
    c.assignMic(1, *cableOut);
    c.waitIdle();
    const auto path = pf8test::scratchRoot() / "livetrace.csv";
    std::ofstream csv(path);
    csv << "t,ppm1,meas1,ring1,avg1,age1,ppm2,meas2,ring2,avg2,age2,status1,status2\n";
    const auto t0 = GetTickCount64();
    while (GetTickCount64() - t0 < 45000)
    {
        Sleep(50);
        const auto s = c.status();
        const auto& a = s.channels[0].mic.bridge;
        const auto& b = s.channels[1].mic.bridge;
        csv << (GetTickCount64() - t0) / 1000.0 << "," << a.ppm << "," << a.measuredNow << "," << a.ringNow << "," << a.fill << ","
            << a.timestampAgeMs << "," << b.ppm << "," << b.measuredNow << "," << b.ringNow << "," << b.fill << "," << b.timestampAgeMs
            << "," << static_cast<int>(a.status) << "," << static_cast<int>(b.status) << "\n";
    }
    std::printf("  trace: %s  target %.0f\n", path.string().c_str(), c.status().channels[0].mic.bridge.target);
}
