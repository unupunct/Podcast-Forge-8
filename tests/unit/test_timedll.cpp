#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <random>

#include "engine/TimeDll.h"

TEST_CASE("TimeDll removes callback jitter and tracks the true period", "[engine][dll]")
{
    pf8::TimeDll dll;
    const double period = 1e9 * 480.0 / 48000.0 * (1.0 + 150e-6); // a master 150 ppm fast
    dll.reset(48000.0, 480.0, 1.0);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(0.0, 500e3); // 0–500 µs late
    double worstAfterSettle = 0.0, sum = 0.0;
    int n = 0;
    for (int k = 0; k < 6000; ++k) // 60 s
    {
        const double ideal = 1e12 + k * period;
        const auto filtered = static_cast<double>(dll.update(static_cast<int64_t>(ideal + jitter(rng)), 480));
        if (k > 1000)
        {
            const double err = filtered - (ideal + 250e3); // mean delay is 250 µs
            worstAfterSettle = std::max(worstAfterSettle, std::abs(err));
            sum += err * err;
            ++n;
        }
    }
    const double rmsUs = std::sqrt(sum / n) * 1e-3;
    std::printf("  DLL: rms error %.2f us, worst %.2f us, period error %.2f ppm\n", rmsUs, worstAfterSettle * 1e-3,
                (dll.nsPerFrame() * 480.0 / period - 1.0) * 1e6);
    CHECK(rmsUs < 60.0); // jitter (≈144 µs rms) reduced > 2.4× at this (high, 1 Hz) bandwidth
    CHECK(std::abs(dll.nsPerFrame() * 480.0 / period - 1.0) < 25e-6); // follows the master's true rate
}
