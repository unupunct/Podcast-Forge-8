#pragma once
// Pre-record buffer (RECORDING.md §7). The tick writes every recordable track into per-track
// circular buffers while no recording runs. On the first block a recording is active, the tick
// freezes the buffer *instead of* writing that block — the block goes to the RecordTap — so the
// pre-roll and the live stream are sample-contiguous. The recorder copies the frozen frames out,
// then releases the buffer, which starts filling again from empty.
//
// Allocation happens only in the constructor (control thread, while the engine does not use it).
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "record/RecordTap.h"

namespace pf8 {

class PreRollBuffer
{
public:
    PreRollBuffer(int sampleRate, double seconds)
        : capacity_(static_cast<uint64_t>(std::max(0.0, seconds) * sampleRate)), seconds_(seconds)
    {
        for (int t = 0; t < kTrackCount; ++t)
            if (recordable(t)) data_[static_cast<size_t>(t)].assign(capacity_ * static_cast<uint64_t>(trackChannels(t)), 0.0f);
    }

    static constexpr bool recordable(int track) noexcept
    {
        return track < 8 || track == static_cast<int>(TrackId::Main) || track == static_cast<int>(TrackId::Music);
    }
    static size_t bytesFor(int sampleRate, double seconds) noexcept
    {
        int ch = 0;
        for (int t = 0; t < kTrackCount; ++t)
            if (recordable(t)) ch += trackChannels(t);
        return static_cast<size_t>(seconds * sampleRate) * static_cast<size_t>(ch) * sizeof(float);
    }

    double seconds() const noexcept { return seconds_; }
    uint64_t capacity() const noexcept { return capacity_; }
    uint64_t filled() const noexcept { return std::min(fill_.load(std::memory_order_acquire), capacity_); }

    // ----- tick thread -----
    // `recording`: the RecordTap is active for this block (read once by the tick).
    void onBlock(const std::array<const float*, kTrackCount>& data, int frames, bool recording) noexcept
    {
        if (frozen_.load(std::memory_order_acquire)) return;
        if (recording)
        {
            if (captureArmed_.load(std::memory_order_acquire))
            {
                frozenFill_ = std::min(fill_.load(std::memory_order_relaxed), capacity_);
                frozen_.store(true, std::memory_order_release);
            }
            return;
        }
        if (capacity_ == 0) return;
        for (int t = 0; t < kTrackCount; ++t)
        {
            auto& buf = data_[static_cast<size_t>(t)];
            if (buf.empty()) continue;
            const int ch = trackChannels(t);
            const float* src = data[static_cast<size_t>(t)];
            uint64_t pos = writePos_;
            int done = 0;
            while (done < frames)
            {
                const int n = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(frames - done), capacity_ - pos));
                float* dst = buf.data() + pos * static_cast<uint64_t>(ch);
                if (src) std::memcpy(dst, src + static_cast<size_t>(done) * static_cast<size_t>(ch), sizeof(float) * static_cast<size_t>(n * ch));
                else std::memset(dst, 0, sizeof(float) * static_cast<size_t>(n * ch));
                done += n;
                pos = (pos + static_cast<uint64_t>(n)) % capacity_;
            }
        }
        writePos_ = (writePos_ + static_cast<uint64_t>(frames)) % capacity_;
        fill_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_release);
    }

    // ----- recorder (control / worker thread) -----
    void armCapture() noexcept { captureArmed_.store(true, std::memory_order_release); }
    bool frozen() const noexcept { return frozen_.load(std::memory_order_acquire); }
    uint64_t frozenFrames() const noexcept { return frozen() ? frozenFill_ : 0; }

    // Copies `frames` frames starting `offset` frames into the frozen pre-roll (0 = oldest).
    void read(int track, uint64_t offset, int frames, float* dst) const noexcept
    {
        const auto& buf = data_[static_cast<size_t>(track)];
        const int ch = trackChannels(track);
        if (buf.empty() || !frozen())
        {
            std::memset(dst, 0, sizeof(float) * static_cast<size_t>(frames * ch));
            return;
        }
        uint64_t pos = (writePos_ + capacity_ - frozenFill_ + offset) % capacity_;
        int done = 0;
        while (done < frames)
        {
            const int n = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(frames - done), capacity_ - pos));
            std::memcpy(dst + static_cast<size_t>(done) * static_cast<size_t>(ch), buf.data() + pos * static_cast<uint64_t>(ch),
                        sizeof(float) * static_cast<size_t>(n * ch));
            done += n;
            pos = (pos + static_cast<uint64_t>(n)) % capacity_;
        }
    }

    // After the recording: empty the buffer and let the tick fill it again.
    void release() noexcept
    {
        captureArmed_.store(false, std::memory_order_release);
        if (!frozen()) return; // the tick never froze it: it is still filling normally
        writePos_ = 0;
        fill_.store(0, std::memory_order_relaxed);
        frozenFill_ = 0;
        frozen_.store(false, std::memory_order_release);
    }

private:
    const uint64_t capacity_;
    const double seconds_;
    std::array<std::vector<float>, kTrackCount> data_;
    uint64_t writePos_ = 0;      // tick (or the recorder while frozen)
    uint64_t frozenFill_ = 0;    // set by the tick before frozen_ is published
    std::atomic<uint64_t> fill_{0};
    std::atomic<bool> captureArmed_{false}, frozen_{false};
};

} // namespace pf8
