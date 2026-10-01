#pragma once
// Single-writer, multi-reader snapshot. The writer never blocks; readers retry when they observe
// a write in progress. T must be trivially copyable.
#include <atomic>
#include <cstring>
#include <immintrin.h>
#include <type_traits>

namespace pf8 {

template <typename T>
class SeqLockSnapshot
{
    static_assert(std::is_trivially_copyable_v<T>, "SeqLockSnapshot requires trivially copyable T");

public:
    void write(const T& v) noexcept
    {
        const unsigned s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(storage_, &v, sizeof(T));
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_release);
    }

    bool tryRead(T& out) const noexcept
    {
        const unsigned s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1u) return false;
        T tmp;
        std::memcpy(&tmp, storage_, sizeof(T));
        std::atomic_thread_fence(std::memory_order_acquire);
        const unsigned s2 = seq_.load(std::memory_order_relaxed);
        if (s1 != s2) return false;
        out = tmp;
        return true;
    }

    // Any number of reader threads. The writer's critical section is one memcpy, so a bounded spin
    // always succeeds in practice; if it ever did not, a value-initialised T ("no data yet") is
    // returned. (No shared fallback copy: several readers writing one would race and tear it.)
    T read() const noexcept
    {
        T out{};
        for (int i = 0; i < 4096; ++i)
        {
            if (tryRead(out)) return out;
            _mm_pause();
        }
        return T{};
    }

private:
    std::atomic<unsigned> seq_{0};
    alignas(64) unsigned char storage_[sizeof(T)]{};
};

} // namespace pf8
