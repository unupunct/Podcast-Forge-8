#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "engine/DriftController.h"

using namespace pf8;

namespace {
// Simulates an input bridge: a device at (1+δ) pushes `devPeriod` frames per callback; the engine
// pulls `block` frames per tick at a ratio corrected by the controller. Returns the controller.
struct Sim
{
    double ppmOffset;
    int devPeriod = 480, block = 128;
    double rate = 48000.0;
    DriftController dc;
    double fill = 0, devClock = 0, engClock = 0, maxAbsErrLast = 0, minFill = 1e9, maxPpm = 0, minPpm = 0, lockedAt = -1;

    void run(double seconds, double target)
    {
        dc.prepare(rate, target, 0.5 * block);
        fill = target - devPeriod; // like a primed bridge: measured == target once the first packet lands
        const double devPeriodSec = devPeriod / (rate * (1.0 + ppmOffset * 1e-6));
        const double blockSec = block / rate;
        double t = 0, nextDev = 0, nextEng = 0, lastDev = 0;
        while (t < seconds)
        {
            if (nextDev <= nextEng)
            {
                t = nextDev;
                fill += devPeriod;
                lastDev = t;
                nextDev += devPeriodSec;
            }
            else
            {
                t = nextEng;
                // Bridges linearise the device staircase: frames the device has accumulated since
                // its last delivery are counted as present (see InputBridge::engineRead).
                const double measured = fill + rate * (t - lastDev);
                const double ppm = dc.update(measured, block);
                fill -= block * (1.0 + ppm * 1e-6);
                minFill = std::min(minFill, fill);
                maxPpm = std::max(maxPpm, ppm);
                minPpm = std::min(minPpm, ppm);
                if (lockedAt < 0 && dc.status() == SyncStatus::Locked) lockedAt = t;
                if (t > seconds - 30) maxAbsErrLast = std::max(maxAbsErrLast, std::abs(dc.averagedFill() - target));
                nextEng += blockSec;
            }
        }
    }
};
} // namespace

TEST_CASE("DriftController locks onto device clock offsets", "[engine][drift]")
{
    for (double offset : {200.0, -150.0, 80.0, 0.0})
    {
        Sim s{offset};
        s.run(120.0, 700.0);
        INFO("offset " << offset << " ppm, estimate " << s.dc.ppm());
        CHECK(s.dc.ppm() == Catch::Approx(offset).margin(5.0));
        CHECK(s.maxAbsErrLast < 0.5 * s.block);
        CHECK(s.dc.status() == SyncStatus::Locked);
        CHECK(s.minFill > 0.0);
    }
}

TEST_CASE("DriftController reports Unstable when the offset exceeds the clamp", "[engine][drift]")
{
    Sim s{1500.0};
    s.run(20.0, 700.0);
    CHECK(s.dc.status() == SyncStatus::Unstable);
    CHECK(std::abs(s.dc.ppm()) == Catch::Approx(DriftController::kMaxPpm));
}

TEST_CASE("DriftController converges without overshoot and locks within 30 s", "[engine][drift]")
{
    for (double offset : {200.0, -150.0})
    {
        Sim s{offset};
        s.run(90.0, 700.0);
        INFO("offset " << offset << " max " << s.maxPpm << " min " << s.minPpm << " locked at " << s.lockedAt);
        // The correction approaches the offset from one side: overshoot ≤ 15 %, no swing to the other sign.
        if (offset > 0)
        {
            CHECK(s.maxPpm < offset * 1.15);
            CHECK(s.minPpm > -0.15 * offset);
        }
        else
        {
            CHECK(s.minPpm > offset * 1.15);
            CHECK(s.maxPpm < -0.15 * offset);
        }
        CHECK(s.lockedAt > 0);
        CHECK(s.lockedAt < 30.0);
    }
}
