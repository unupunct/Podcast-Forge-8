#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "engine/Bridges.h"

using namespace pf8;

namespace {
BridgeConfig cfg(bool master = false, int devCh = 1, int period = 480)
{
    BridgeConfig c;
    c.deviceChannels = devCh;
    c.devicePeriod = period;
    c.master = master;
    return c;
}
constexpr int64_t kMs = 1'000'000;
} // namespace

TEST_CASE("InputBridge primes with silence, then passes a DC level after the fade", "[engine][bridges]")
{
    InputBridge b(cfg());
    std::vector<float> dev(480, 0.5f), out(128);
    float* outs[1] = {out.data()};
    int64_t t = 0;

    b.engineRead(outs, 128, t);
    CHECK(out[0] == 0.0f);
    CHECK(b.read().status == SyncStatus::Priming);

    // Feed 480-frame packets every 10 ms; read 128-frame blocks as fast as the device supplies them.
    double lastValue = 0;
    int reads = 0;
    int64_t devFrames = 0, engFrames = 0;
    for (int k = 0; k < 400; ++k)
    {
        b.deviceWrite(dev.data(), 480, t);
        t += 10 * kMs;
        devFrames += 480;
        while (engFrames + 128 <= devFrames)
        {
            b.engineRead(outs, 128, t);
            lastValue = out[127];
            engFrames += 128;
            ++reads;
        }
    }
    CHECK(reads > 100);
    CHECK(lastValue == Catch::Approx(0.5).margin(1e-3));
    CHECK(b.read().underruns == 0);
}

TEST_CASE("InputBridge counts an underrun once and re-primes", "[engine][bridges]")
{
    InputBridge b(cfg());
    std::vector<float> dev(480, 0.25f), out(128);
    float* outs[1] = {out.data()};
    for (int k = 0; k < 4; ++k) b.deviceWrite(dev.data(), 480, k * 10 * kMs);
    for (int k = 0; k < 40; ++k) b.engineRead(outs, 128, 50 * kMs); // device stopped delivering
    CHECK(b.read().underruns == 1);
    CHECK(b.read().status == SyncStatus::Priming);
    CHECK(out[0] == 0.0f);
}

TEST_CASE("InputBridge reports overruns when the engine stops reading", "[engine][bridges]")
{
    InputBridge b(cfg());
    std::vector<float> dev(480, 0.1f);
    for (int k = 0; k < 400; ++k) b.deviceWrite(dev.data(), 480, k * 10 * kMs);
    CHECK(b.read().overruns > 0);
    CHECK(b.read().droppedFrames > 0);
}

TEST_CASE("OutputBridge maps a stereo pair to a mono device as (L+R)/2", "[engine][bridges]")
{
    OutputBridge b(cfg(false, 1, 480));
    std::vector<float> l(128, 1.0f), r(128, 0.0f), dev(480);
    int64_t t = 0;
    float last = 0;
    for (int k = 0; k < 2000; ++k)
    {
        b.engineWritePair(0, l.data(), r.data(), 128);
        b.engineCommit(128, t);
        t += 128 * 1'000'000'000LL / 48000;
        if (k % 4 == 3) // 4 × 128 = 512 engine frames per ~480 device frames
        {
            b.deviceRead(dev.data(), 480, t);
            last = dev[479];
        }
    }
    CHECK(last == Catch::Approx(0.5).margin(1e-3));
}

namespace {
struct Driver : TickClient
{
    OutputBridge* bridge = nullptr;
    int ticks = 0;
    std::vector<float> l = std::vector<float>(512, 0.3f), r = std::vector<float>(512, -0.3f);
    void tick(int n) noexcept override
    {
        ++ticks;
        bridge->engineWritePair(0, l.data(), r.data(), n);
        bridge->engineCommit(n, 0);
    }
};
} // namespace

TEST_CASE("Master OutputBridge ticks on demand and adds at most one block of latency", "[engine][bridges]")
{
    OutputBridge b(cfg(true, 2, 480));
    Driver d;
    d.bridge = &b;
    std::vector<float> dev(480 * 2);
    for (int k = 0; k < 100; ++k)
    {
        b.deviceRead(dev.data(), 480, 0, &d);
        CHECK(b.readableDeviceFrames() < 128);
    }
    CHECK(b.read().underruns == 0);
    CHECK(dev[0] == Catch::Approx(0.3f));
    CHECK(dev[1] == Catch::Approx(-0.3f));
    CHECK(d.ticks >= 100 * 480 / 128);
}
