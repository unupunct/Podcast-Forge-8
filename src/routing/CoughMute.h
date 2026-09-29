#pragma once
// Cough button state machine (ROUTING.md §4). Driven by key down/up events from HotkeyManager on
// the UI thread; its output is the channel's `coughOpen` flag (the engine ramps it over 5 ms).
#include "routing/RoutingTypes.h"

namespace pf8 {

class CoughMute
{
public:
    explicit CoughMute(CoughMode mode = CoughMode::PushToMute) { setMode(mode); }

    void setMode(CoughMode mode) noexcept
    {
        mode_ = mode;
        held_ = false;
        toggledClosed_ = false;
    }
    CoughMode mode() const noexcept { return mode_; }

    void keyDown() noexcept
    {
        if (held_) return; // key auto-repeat
        held_ = true;
        if (mode_ == CoughMode::Toggle) toggledClosed_ = !toggledClosed_;
    }

    void keyUp() noexcept { held_ = false; }

    // Focus lost while a key was held: treat as released so a channel can't stay stuck.
    void releaseAll() noexcept { held_ = false; }

    bool open() const noexcept
    {
        switch (mode_)
        {
            case CoughMode::PushToMute: return !held_;
            case CoughMode::PushToTalk: return held_;
            case CoughMode::Toggle: return !toggledClosed_;
        }
        return true;
    }

private:
    CoughMode mode_ = CoughMode::PushToMute;
    bool held_ = false;
    bool toggledClosed_ = false;
};

} // namespace pf8
