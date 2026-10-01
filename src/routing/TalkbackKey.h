#pragma once
// Talkback key behaviour (brief §16). UI / hotkey thread only; the result goes to
// RoutingParams::talkbackActive.
//
//   Auto      — hold to talk; a short tap (< tapMs) latches, the next press unlatches.
//   Momentary — talk only while held.
//   Latch     — every press toggles.
#include <cstdint>

namespace pf8 {

enum class TalkbackMode : uint8_t { Auto = 0, Momentary, Latch };

class TalkbackKey
{
public:
    static constexpr int64_t kTapMs = 300;

    void setMode(TalkbackMode m) noexcept
    {
        mode_ = m;
        if (m == TalkbackMode::Momentary && latched_) reset();
    }
    TalkbackMode mode() const noexcept { return mode_; }

    void press(int64_t nowMs) noexcept
    {
        if (down_) return; // key repeat
        down_ = true;
        if (latched_)
        {
            latched_ = false;
            active_ = false;
            swallowRelease_ = true;
            return;
        }
        active_ = true;
        pressedAt_ = nowMs;
        if (mode_ == TalkbackMode::Latch)
        {
            latched_ = true;
            swallowRelease_ = true;
        }
    }

    void release(int64_t nowMs) noexcept
    {
        if (!down_) return;
        down_ = false;
        if (swallowRelease_)
        {
            swallowRelease_ = false;
            return;
        }
        if (mode_ == TalkbackMode::Auto && nowMs - pressedAt_ < kTapMs)
            latched_ = true; // stays on
        else
            active_ = false;
    }

    void reset() noexcept
    {
        down_ = latched_ = active_ = swallowRelease_ = false;
    }

    bool active() const noexcept { return active_; }
    bool latched() const noexcept { return latched_; }

private:
    TalkbackMode mode_ = TalkbackMode::Auto;
    bool down_ = false, latched_ = false, active_ = false, swallowRelease_ = false;
    int64_t pressedAt_ = 0;
};

} // namespace pf8
