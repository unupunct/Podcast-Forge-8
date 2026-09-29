#pragma once
// Monotonic wall clock in nanoseconds (QueryPerformanceCounter). Real-time safe.
#include <cstdint>

namespace pf8 {

int64_t monotonicNs() noexcept;

} // namespace pf8
