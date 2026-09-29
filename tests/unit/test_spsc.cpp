#include <catch2/catch_test_macros.hpp>

#include <random>
#include <thread>
#include <vector>

#include "core/SpscRing.h"

using pf8::SpscRing;

TEST_CASE("SpscRing capacity rounds up to power of two", "[core]")
{
    SpscRing<float> r(100);
    REQUIRE(r.capacity() == 128);
    REQUIRE(r.size() == 0);
    REQUIRE(r.freeSpace() == 128);
}

TEST_CASE("SpscRing keeps order across wrap-around", "[core]")
{
    SpscRing<int> r(64);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> len(0, 40);
    int nextWrite = 0, nextRead = 0;
    std::vector<int> buf(64);
    for (int iter = 0; iter < 10000; ++iter)
    {
        const int n = len(rng);
        for (int i = 0; i < n; ++i) buf[i] = nextWrite + i;
        nextWrite += static_cast<int>(r.push(buf.data(), n));
        const int m = len(rng);
        const size_t got = r.pop(buf.data(), m);
        for (size_t i = 0; i < got; ++i) REQUIRE(buf[i] == nextRead++);
    }
}

TEST_CASE("SpscRing push on full ring returns zero, peek and discard", "[core]")
{
    SpscRing<int> r(4);
    int v[4] = {1, 2, 3, 4};
    REQUIRE(r.push(v, 4) == 4);
    REQUIRE(r.push(v, 1) == 0);
    int p[2]{};
    REQUIRE(r.peek(p, 2) == 2);
    REQUIRE(p[0] == 1);
    REQUIRE(r.size() == 4);
    REQUIRE(r.discard(3) == 3);
    REQUIRE(r.pop(p, 2) == 1);
    REQUIRE(p[0] == 4);
}

TEST_CASE("SpscRing two-thread stress preserves the sequence", "[core]")
{
    SpscRing<uint32_t> r(1024);
    constexpr uint32_t total = 5'000'000;
    std::thread producer([&] {
        uint32_t next = 0, chunk[37];
        while (next < total)
        {
            uint32_t n = 0;
            while (n < 37 && next + n < total) { chunk[n] = next + n; ++n; }
            next += static_cast<uint32_t>(r.push(chunk, n));
        }
    });
    uint32_t expected = 0, buf[53];
    bool ok = true;
    while (expected < total)
    {
        const size_t got = r.pop(buf, 53);
        for (size_t i = 0; i < got; ++i) ok &= (buf[i] == expected++);
    }
    producer.join();
    REQUIRE(ok);
}
