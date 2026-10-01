#pragma once
// Tick-stall detection (AUDIO_ENGINE.md §6): the engine tick must advance at least every
// `stallMs`. Pure logic, driven by the controller's watchdog thread.
#include <cstdint>

namespace pf8 {

class TickWatchdog
{
public:
    explicit TickWatchdog(int64_t stallMs = 500, int maxRestartsPerMinute = 3) : stallMs_(stallMs), maxPerMinute_(maxRestartsPerMinute) {}

    enum class Verdict : uint8_t { Ok, Stalled, StalledNoRestart };

    // `ticks`: the engine's tick counter. `grace`: the control thread is busy (opening / closing
    // streams legitimately pauses the clock) — the stall timer restarts.
    Verdict update(uint64_t ticks, int64_t nowMs, bool grace) noexcept
    {
        if (ticks != lastTicks_ || grace || lastChange_ < 0)
        {
            lastTicks_ = ticks;
            lastChange_ = nowMs;
            reported_ = false;
            return Verdict::Ok;
        }
        if (reported_ || nowMs - lastChange_ < stallMs_) return Verdict::Ok;
        reported_ = true; // once per stall
        ++stalls_;
        // Rate limit: at most N restarts in any 60 s window.
        if (windowStart_ < 0 || nowMs - windowStart_ >= 60000)
        {
            windowStart_ = nowMs;
            inWindow_ = 0;
        }
        if (inWindow_ >= maxPerMinute_) return Verdict::StalledNoRestart;
        ++inWindow_;
        ++restarts_;
        return Verdict::Stalled;
    }

    uint64_t stalls() const noexcept { return stalls_; }
    uint64_t restarts() const noexcept { return restarts_; }
    int64_t stalledForMs(int64_t nowMs) const noexcept { return lastChange_ < 0 ? 0 : nowMs - lastChange_; }

private:
    int64_t stallMs_;
    int maxPerMinute_;
    uint64_t lastTicks_ = 0;
    int64_t lastChange_ = -1;
    bool reported_ = false;
    uint64_t stalls_ = 0, restarts_ = 0;
    int64_t windowStart_ = -1;
    int inWindow_ = 0;
};

} // namespace pf8
