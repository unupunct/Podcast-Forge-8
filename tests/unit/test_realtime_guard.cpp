#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "core/RealtimeGuard.h"

TEST_CASE("RealtimeGuard counts allocations only on realtime threads", "[core]")
{
    pf8::rt::resetCounters();
    {
        auto outside = std::make_unique<int>(1);
        REQUIRE(pf8::rt::allocationsOnRealtimeThreads() == 0);
    }
    {
        pf8::rt::ScopedRealtime rt;
        REQUIRE(pf8::rt::isRealtimeThread());
        auto inside = std::make_unique<int>(2);
        REQUIRE(pf8::rt::allocationsOnRealtimeThreads() == 1);
    }
    REQUIRE_FALSE(pf8::rt::isRealtimeThread());
    pf8::rt::resetCounters();
}
