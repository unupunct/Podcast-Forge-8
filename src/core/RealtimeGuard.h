#pragma once
// Marks threads as real-time so test builds can count heap allocations made on them.
#include <cstdint>

namespace pf8::rt {

struct ScopedRealtime
{
    ScopedRealtime() noexcept;
    ~ScopedRealtime() noexcept;
    ScopedRealtime(const ScopedRealtime&) = delete;
    ScopedRealtime& operator=(const ScopedRealtime&) = delete;
};

bool isRealtimeThread() noexcept;
void noteAllocation() noexcept; // called by the test allocator hook
uint64_t allocationsOnRealtimeThreads() noexcept;
void resetCounters() noexcept;

} // namespace pf8::rt
