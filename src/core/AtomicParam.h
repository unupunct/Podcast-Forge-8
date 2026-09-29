#pragma once
// A float parameter written by the UI and read by the audio thread. Relaxed ordering: every
// parameter is independent and is smoothed on the audio side.
#include <atomic>

namespace pf8 {

class AtomicParam
{
public:
    explicit AtomicParam(float v = 0.0f) noexcept : value_(v) {}
    AtomicParam(const AtomicParam& o) noexcept : value_(o.get()) {}
    AtomicParam& operator=(const AtomicParam& o) noexcept { set(o.get()); return *this; }

    float get() const noexcept { return value_.load(std::memory_order_relaxed); }
    void set(float v) noexcept { value_.store(v, std::memory_order_relaxed); }

private:
    static_assert(std::atomic<float>::is_always_lock_free);
    std::atomic<float> value_;
};

} // namespace pf8
