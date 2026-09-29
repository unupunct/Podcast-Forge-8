#pragma once
// Bounded multi-producer / single-consumer queue (sequence-cell design by D. Vyukov).
// tryPush and tryPop never block and never allocate.
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

#include "core/SpscRing.h"

namespace pf8 {

template <typename T>
class MpscQueue
{
public:
    explicit MpscQueue(size_t minCapacity)
        : capacity_(nextPowerOfTwo(minCapacity < 2 ? 2 : minCapacity)),
          mask_(capacity_ - 1),
          cells_(std::make_unique<Cell[]>(capacity_))
    {
        for (size_t i = 0; i < capacity_; ++i)
            cells_[i].sequence.store(i, std::memory_order_relaxed);
    }

    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;

    size_t capacity() const noexcept { return capacity_; }

    bool tryPush(const T& v) noexcept
    {
        Cell* cell = claim();
        if (cell == nullptr) return false;
        cell->value = v;
        publish(cell);
        return true;
    }

    bool tryPush(T&& v) noexcept
    {
        Cell* cell = claim();
        if (cell == nullptr) return false;
        cell->value = std::move(v);
        publish(cell);
        return true;
    }

    bool tryPop(T& out) noexcept
    {
        Cell& cell = cells_[dequeuePos_ & mask_];
        const size_t seq = cell.sequence.load(std::memory_order_acquire);
        const auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(dequeuePos_ + 1);
        if (diff < 0) return false; // empty
        out = std::move(cell.value);
        cell.sequence.store(dequeuePos_ + capacity_, std::memory_order_release);
        ++dequeuePos_;
        return true;
    }

private:
    struct Cell
    {
        std::atomic<size_t> sequence{0};
        size_t claimedPos = 0;
        T value{};
    };

    Cell* claim() noexcept
    {
        size_t pos = enqueuePos_.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell& cell = cells_[pos & mask_];
            const size_t seq = cell.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);
            if (diff == 0)
            {
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    cell.claimedPos = pos;
                    return &cell;
                }
            }
            else if (diff < 0)
            {
                return nullptr; // full
            }
            else
            {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
    }

    static void publish(Cell* cell) noexcept
    {
        cell->sequence.store(cell->claimedPos + 1, std::memory_order_release);
    }

    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<Cell[]> cells_;
    alignas(64) std::atomic<size_t> enqueuePos_{0};
    alignas(64) size_t dequeuePos_ = 0;
};

} // namespace pf8
