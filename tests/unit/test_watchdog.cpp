#include <catch2/catch_test_macros.hpp>

#include "engine/Watchdog.h"

using namespace pf8;

TEST_CASE("Watchdog: a stall is reported once, busy periods are grace, restarts are rate-limited", "[engine][watchdog]")
{
    TickWatchdog w(500, 3);
    uint64_t ticks = 0;
    int64_t t = 0;
    for (; t < 2000; t += 100) CHECK(w.update(++ticks, t, false) == TickWatchdog::Verdict::Ok);
    // Ticks stop: nothing until 500 ms have passed, then exactly one report.
    CHECK(w.update(ticks, t += 100, false) == TickWatchdog::Verdict::Ok);
    CHECK(w.update(ticks, t += 200, false) == TickWatchdog::Verdict::Ok); // 400 ms since the last tick
    CHECK(w.update(ticks, t += 200, false) == TickWatchdog::Verdict::Stalled);
    for (int i = 0; i < 10; ++i) CHECK(w.update(ticks, t += 100, false) == TickWatchdog::Verdict::Ok);
    CHECK(w.stalls() == 1);
    // While the control thread is busy (streams opening), a pause is not a stall.
    for (int i = 0; i < 20; ++i) CHECK(w.update(++ticks, t += 100, false) == TickWatchdog::Verdict::Ok);
    for (int i = 0; i < 20; ++i) CHECK(w.update(ticks, t += 100, true) == TickWatchdog::Verdict::Ok);
    // Repeated stalls within a minute: the 4th is reported without a restart.
    int restarts = 0, limited = 0;
    for (int k = 0; k < 4; ++k)
    {
        w.update(++ticks, t += 100, false);
        TickWatchdog::Verdict v = TickWatchdog::Verdict::Ok;
        for (int i = 0; i < 7 && v == TickWatchdog::Verdict::Ok; ++i) v = w.update(ticks, t += 100, false);
        restarts += v == TickWatchdog::Verdict::Stalled;
        limited += v == TickWatchdog::Verdict::StalledNoRestart;
    }
    CHECK(restarts == 2); // 1 earlier + 2 = the limit of 3 per minute
    CHECK(limited == 2);
    // A minute later restarts are allowed again.
    w.update(++ticks, t += 61000, false);
    TickWatchdog::Verdict v = TickWatchdog::Verdict::Ok;
    for (int i = 0; i < 7 && v == TickWatchdog::Verdict::Ok; ++i) v = w.update(ticks, t += 100, false);
    CHECK(v == TickWatchdog::Verdict::Stalled);
}
