#pragma once
// Single-producer / single-consumer lock-free ring buffer.
// The producer and consumer may be different threads; each side must be used by one thread only.
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

namespace pf8 {

inline size_t nextPowerOfTwo(size_t v) noexcept
{
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

template <typename T>
class SpscRing
{
    static_assert(std::is_trivially_copyable_v<T>, "SpscRing requires trivially copyable T");

public:
    explicit SpscRing(size_t minCapacity)
        : capacity_(nextPowerOfTwo(minCapacity < 2 ? 2 : minCapacity)),
          mask_(capacity_ - 1),
          data_(std::make_unique<T[]>(capacity_))
    {
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    size_t capacity() const noexcept { return capacity_; }

    size_t size() const noexcept
    {
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t head = head_.load(std::memory_order_acquire);
        return head - tail;
    }

    size_t freeSpace() const noexcept { return capacity_ - size(); }

    size_t push(const T* src, size_t n) noexcept
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t space = capacity_ - (head - tail);
        if (n > space) n = space;
        copyIn(head, src, n);
        head_.store(head + n, std::memory_order_release);
        return n;
    }

    size_t pop(T* dst, size_t n) noexcept
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t avail = head - tail;
        if (n > avail) n = avail;
        copyOut(tail, dst, n);
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // Copies up to n readable items without consuming them (consumer side).
    size_t peek(T* dst, size_t n) const noexcept
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t avail = head - tail;
        if (n > avail) n = avail;
        copyOut(tail, dst, n);
        return n;
    }

    size_t discard(size_t n) noexcept
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t avail = head - tail;
        if (n > avail) n = avail;
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // Only valid while neither side is active.
    void reset() noexcept
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    void copyIn(size_t pos, const T* src, size_t n) noexcept
    {
        if (n == 0) return;
        const size_t start = pos & mask_;
        const size_t first = n < capacity_ - start ? n : capacity_ - start;
        std::memcpy(data_.get() + start, src, first * sizeof(T));
        if (n > first) std::memcpy(data_.get(), src + first, (n - first) * sizeof(T));
    }

    void copyOut(size_t pos, T* dst, size_t n) const noexcept
    {
        if (n == 0) return;
        const size_t start = pos & mask_;
        const size_t first = n < capacity_ - start ? n : capacity_ - start;
        std::memcpy(dst, data_.get() + start, first * sizeof(T));
        if (n > first) std::memcpy(dst + first, data_.get(), (n - first) * sizeof(T));
    }

    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<T[]> data_;
    alignas(64) std::atomic<size_t> head_{0}; // written by the producer
    alignas(64) std::atomic<size_t> tail_{0}; // written by the consumer
};

} // namespace pf8
