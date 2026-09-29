#include "core/RealtimeGuard.h"

#include <atomic>

namespace pf8::rt {
namespace {
thread_local int depth = 0;
std::atomic<uint64_t> allocations{0};
} // namespace

ScopedRealtime::ScopedRealtime() noexcept { ++depth; }
ScopedRealtime::~ScopedRealtime() noexcept { --depth; }

bool isRealtimeThread() noexcept { return depth > 0; }

void noteAllocation() noexcept
{
    if (depth > 0) allocations.fetch_add(1, std::memory_order_relaxed);
}

uint64_t allocationsOnRealtimeThreads() noexcept { return allocations.load(); }
void resetCounters() noexcept { allocations.store(0); }

} // namespace pf8::rt
