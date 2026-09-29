#include <catch2/catch_test_macros.hpp>

#include <array>
#include <thread>
#include <vector>

#include "core/MpscQueue.h"

TEST_CASE("MpscQueue reports full and empty", "[core]")
{
    pf8::MpscQueue<int> q(4);
    int out = 0;
    REQUIRE_FALSE(q.tryPop(out));
    for (int i = 0; i < 4; ++i) REQUIRE(q.tryPush(i));
    REQUIRE_FALSE(q.tryPush(99));
    REQUIRE(q.tryPop(out));
    REQUIRE(out == 0);
    REQUIRE(q.tryPush(4));
}

TEST_CASE("MpscQueue four producers keep per-producer order", "[core]")
{
    struct Item { uint32_t producer; uint32_t seq; };
    pf8::MpscQueue<Item> q(1024);
    constexpr uint32_t perProducer = 250'000;
    std::vector<std::thread> producers;
    for (uint32_t p = 0; p < 4; ++p)
        producers.emplace_back([&q, p] {
            for (uint32_t s = 0; s < perProducer;)
                if (q.tryPush(Item{p, s})) ++s;
        });

    std::array<uint32_t, 4> next{};
    uint32_t received = 0;
    bool ordered = true;
    Item it{};
    while (received < 4 * perProducer)
    {
        if (q.tryPop(it))
        {
            ordered &= (it.seq == next[it.producer]);
            ++next[it.producer];
            ++received;
        }
    }
    for (auto& t : producers) t.join();
    REQUIRE(ordered);
    REQUIRE(received == 4 * perProducer);
}
