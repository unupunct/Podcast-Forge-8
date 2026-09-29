#pragma once
// Linear gain ramp: moves to a new target over a fixed number of samples. Real-time safe.
#include <cmath>

namespace pf8 {

class Ramp
{
public:
    void setLength(int samples) noexcept { length_ = samples > 0 ? samples : 1; }
    void reset(float value) noexcept
    {
        current_ = target_ = value;
        step_ = 0.0f;
        remaining_ = 0;
    }

    void setTarget(float t) noexcept
    {
        if (t == target_) return;
        target_ = t;
        remaining_ = length_;
        step_ = (target_ - current_) / static_cast<float>(length_);
    }

    float target() const noexcept { return target_; }
    float current() const noexcept { return current_; }
    bool ramping() const noexcept { return remaining_ > 0; }

    float next() noexcept
    {
        if (remaining_ > 0)
        {
            current_ += step_;
            if (--remaining_ == 0) current_ = target_;
        }
        return current_;
    }

    // Fills `out` with the next n gain values (for vectorised mixing).
    void fill(float* out, int n) noexcept
    {
        if (remaining_ == 0)
        {
            for (int i = 0; i < n; ++i) out[i] = current_;
            return;
        }
        for (int i = 0; i < n; ++i) out[i] = next();
    }

private:
    float current_ = 0.0f, target_ = 0.0f, step_ = 0.0f;
    int remaining_ = 0;
    int length_ = 480;
};

inline float dbToGain(float db) noexcept { return db <= -120.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g) noexcept { return g <= 1e-6f ? -120.0f : 20.0f * std::log10(g); }

} // namespace pf8
