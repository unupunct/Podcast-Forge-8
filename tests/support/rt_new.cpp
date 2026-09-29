// Test-only global allocator hooks: count allocations made on threads marked real-time.
#include <cstdlib>
#include <new>

#include "core/RealtimeGuard.h"

void* operator new(std::size_t size)
{
    pf8::rt::noteAllocation();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    pf8::rt::noteAllocation();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    pf8::rt::noteAllocation();
    return std::malloc(size ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    pf8::rt::noteAllocation();
    return std::malloc(size ? size : 1);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
