#include "core/Clock.h"

#include <windows.h>

namespace pf8 {

int64_t monotonicNs() noexcept
{
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    // Split to avoid overflow: (c / f) seconds + remainder.
    const int64_t s = c.QuadPart / freq;
    const int64_t r = c.QuadPart % freq;
    return s * 1'000'000'000 + r * 1'000'000'000 / freq;
}

} // namespace pf8
