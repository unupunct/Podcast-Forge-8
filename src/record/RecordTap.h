#pragma once
// The engine → recorder hand-off (RECORDING.md §3). The recorder configures the tap on the control
// thread while inactive; the tick then pushes every armed track's block into its own SPSC ring,
// all-or-nothing, so tracks can never drift apart. Nothing here allocates once configured.
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "core/SpscRing.h"

namespace pf8 {

enum class TrackId : uint8_t
{
    Ch1 = 0, Ch2, Ch3, Ch4, Ch5, Ch6, Ch7, Ch8,
    Main = 8,   // stereo, post master limiter
    System = 9, // stereo, WASAPI loopback (reserved)
    Music = 10, // stereo (Stage 9)
    Remote = 11 // mono (reserved)
};
constexpr int kTrackCount = 12;
constexpr int trackChannels(int track) noexcept { return track == 8 || track == 9 || track == 10 ? 2 : 1; }

class RecordTap
{
public:
    // Control thread, tap inactive.
    void configure(uint32_t trackMask, int sampleRate, double bufferSeconds)
    {
        mask_ = trackMask;
        for (int t = 0; t < kTrackCount; ++t)
        {
            if (mask_ & (1u << t))
                rings_[static_cast<size_t>(t)] = std::make_unique<SpscRing<float>>(
                    static_cast<size_t>(bufferSeconds * sampleRate) * static_cast<size_t>(trackChannels(t)));
            else
                rings_[static_cast<size_t>(t)].reset();
        }
        framesPushed_.store(0);
        framesDropped_.store(0);
        dropEvents_.store(0);
    }

    uint32_t mask() const noexcept { return mask_; }
    SpscRing<float>* ring(int track) noexcept { return rings_[static_cast<size_t>(track)].get(); }

    void setActive(bool on) noexcept { active_.store(on, std::memory_order_release); }
    bool active() const noexcept { return active_.load(std::memory_order_acquire); }
    void setPaused(bool on) noexcept { paused_.store(on, std::memory_order_release); }
    bool paused() const noexcept { return paused_.load(std::memory_order_acquire); }

    // Tick: data[track] points at `frames` frames (interleaved for stereo tracks); unused tracks nullptr.
    // Returns false (and counts a dropout) when any ring lacks space — the whole block is dropped.
    bool push(const std::array<const float*, kTrackCount>& data, int frames) noexcept
    {
        if (!active() || paused()) return true;
        for (int t = 0; t < kTrackCount; ++t)
            if ((mask_ & (1u << t)) && rings_[static_cast<size_t>(t)] &&
                rings_[static_cast<size_t>(t)]->freeSpace() < static_cast<size_t>(frames * trackChannels(t)))
            {
                framesDropped_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_relaxed);
                dropEvents_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        for (int t = 0; t < kTrackCount; ++t)
        {
            if (!(mask_ & (1u << t)) || !rings_[static_cast<size_t>(t)]) continue;
            const size_t n = static_cast<size_t>(frames * trackChannels(t));
            if (data[static_cast<size_t>(t)]) rings_[static_cast<size_t>(t)]->push(data[static_cast<size_t>(t)], n);
            else
            {
                // Reserved track without a source yet: silence keeps it aligned.
                static const float zeros[1024] = {};
                size_t left = n;
                while (left > 0)
                {
                    const size_t c = left < 1024 ? left : 1024;
                    rings_[static_cast<size_t>(t)]->push(zeros, c);
                    left -= c;
                }
            }
        }
        framesPushed_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_release);
        return true;
    }

    uint64_t framesPushed() const noexcept { return framesPushed_.load(std::memory_order_acquire); }
    uint64_t framesDropped() const noexcept { return framesDropped_.load(std::memory_order_relaxed); }
    uint64_t dropEvents() const noexcept { return dropEvents_.load(std::memory_order_relaxed); }

private:
    uint32_t mask_ = 0;
    std::array<std::unique_ptr<SpscRing<float>>, kTrackCount> rings_;
    std::atomic<bool> active_{false}, paused_{false};
    std::atomic<uint64_t> framesPushed_{0}, framesDropped_{0}, dropEvents_{0};
};

} // namespace pf8
