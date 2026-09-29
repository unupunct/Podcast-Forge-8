#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <atomic>

#include "engine/InternalClock.h"
#include "engine/MasterClock.h"

using namespace pf8;

TEST_CASE("selectMaster follows the documented priority", "[engine]")
{
    std::vector<MasterCandidate> c = {
        {"mic1", Flow::Capture, true, false, false, 0},
        {"hp2", Flow::Render, true, false, true, 2},
        {"hp1", Flow::Render, true, false, true, 1},
        {"spk", Flow::Render, true, false, false, 9},
    };
    CHECK(selectMaster(c) == "hp1");                 // first online headphone by order

    c[3].userPreferred = true;
    CHECK(selectMaster(c) == "spk");                 // user preference wins

    c[3].online = false;
    CHECK(selectMaster(c) == "hp1");                 // preferred but offline is skipped

    c[1].online = c[2].online = false;
    CHECK(selectMaster(c) == "mic1");                // only an input left

    c[0].online = false;
    CHECK_FALSE(selectMaster(c).has_value());        // nothing online → internal clock
}

namespace {
struct CountingClient : TickClient
{
    std::atomic<int> ticks{0};
    std::atomic<int> frames{0};
    void tick(int n) noexcept override
    {
        ticks.fetch_add(1);
        frames.fetch_add(n);
    }
};
} // namespace

TEST_CASE("InternalClock ticks at the block rate", "[engine]")
{
    CountingClient client;
    InternalClock clock(&client, 48000, 480);  // 10 ms blocks
    clock.start();
    Sleep(1000);
    clock.stop();
    CHECK(client.ticks.load() >= 95);
    CHECK(client.ticks.load() <= 105);
    CHECK(client.frames.load() == client.ticks.load() * 480);
}
