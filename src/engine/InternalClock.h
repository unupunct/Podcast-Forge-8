#pragma once
// Fallback master clock: an MMCSS thread on a high-resolution waitable timer that calls
// TickClient::tick(blockFrames) every block period. Scheduling is absolute (QPC based) so the
// long-term tick rate is exact even when individual wakeups jitter.
#include <atomic>
#include <cstdint>
#include <thread>

#include "engine/StreamTypes.h"

namespace pf8 {

class InternalClock
{
public:
    InternalClock(TickClient* client, int sampleRate, int blockFrames);
    ~InternalClock();
    InternalClock(const InternalClock&) = delete;
    InternalClock& operator=(const InternalClock&) = delete;

    void start();
    void stop();
    bool running() const noexcept { return running_.load(); }
    uint64_t ticks() const noexcept { return ticks_.load(); }
    uint64_t lateTicks() const noexcept { return late_.load(); } // woke > 1 block late

private:
    void threadMain();

    TickClient* client_;
    int sampleRate_;
    int blockFrames_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> ticks_{0};
    std::atomic<uint64_t> late_{0};
};

} // namespace pf8
