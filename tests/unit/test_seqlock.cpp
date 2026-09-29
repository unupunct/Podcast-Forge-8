#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "core/AtomicParam.h"
#include "core/SeqLock.h"

TEST_CASE("SeqLockSnapshot readers never see a torn value", "[core]")
{
    struct Pair { int64_t a; int64_t b; int64_t pad[6]; };
    pf8::SeqLockSnapshot<Pair> snap;
    snap.write(Pair{0, 0, {}});
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    std::atomic<int64_t> reads{0};

    std::thread reader([&] {
        Pair p{};
        while (!done.load())
            if (snap.tryRead(p))
            {
                if (p.a != -p.b) torn.fetch_add(1);
                reads.fetch_add(1);
            }
    });
    for (int64_t i = 1; i <= 1'000'000; ++i) snap.write(Pair{i, -i, {}});
    done.store(true);
    reader.join();
    REQUIRE(torn.load() == 0);
    REQUIRE(reads.load() > 0);
    REQUIRE(snap.read().a == 1'000'000);
}

TEST_CASE("AtomicParam stores and loads", "[core]")
{
    pf8::AtomicParam p(0.5f);
    REQUIRE(p.get() == 0.5f);
    p.set(-3.0f);
    REQUIRE(p.get() == -3.0f);
}
