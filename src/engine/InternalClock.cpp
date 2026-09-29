#include "engine/InternalClock.h"

#include <windows.h>
#include <avrt.h>
#include <immintrin.h>

#include "core/RealtimeGuard.h"

namespace pf8 {

InternalClock::InternalClock(TickClient* client, int sampleRate, int blockFrames)
    : client_(client), sampleRate_(sampleRate), blockFrames_(blockFrames)
{
}

InternalClock::~InternalClock() { stop(); }

void InternalClock::start()
{
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { threadMain(); });
}

void InternalClock::stop()
{
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void InternalClock::threadMain()
{
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    _mm_setcsr(_mm_getcsr() | 0x8040);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    const double ticksPerBlock = static_cast<double>(freq.QuadPart) * blockFrames_ / sampleRate_;
    const int64_t start = now.QuadPart;
    uint64_t n = 0;

    rt::ScopedRealtime rtScope;
    while (running_.load(std::memory_order_relaxed))
    {
        const int64_t due = start + static_cast<int64_t>(ticksPerBlock * static_cast<double>(n + 1));
        QueryPerformanceCounter(&now);
        const int64_t remaining = due - now.QuadPart;
        if (remaining > 0)
        {
            LARGE_INTEGER rel;
            rel.QuadPart = -static_cast<LONGLONG>(remaining * 10'000'000 / freq.QuadPart); // relative, 100 ns
            if (rel.QuadPart < 0 && SetWaitableTimer(timer, &rel, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(timer, 1000);
        }
        else if (-remaining > static_cast<int64_t>(ticksPerBlock))
        {
            late_.fetch_add(1, std::memory_order_relaxed);
        }
        client_->tick(blockFrames_);
        ++n;
        ticks_.store(n, std::memory_order_relaxed);
    }

    if (timer) CloseHandle(timer);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
}

} // namespace pf8
