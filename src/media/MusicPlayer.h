#pragma once
// Music channel (brief §15): a playlist streamed from disk. A decoder thread reads and resamples
// into a lock-free ring; the audio thread only pops from it. Real-time safe on the audio side.
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/AtomicParam.h"
#include "core/SpscRing.h"

namespace pf8 {

struct MusicTrackInfo
{
    std::filesystem::path path;
    std::string title;
    double seconds = 0.0; // 0 until known
    std::string error;
};

class MusicPlayer
{
public:
    explicit MusicPlayer(int sampleRate);
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    void setSampleRate(int r) noexcept { rate_ = r; }

    // ----- control / UI thread -----
    void setPlaylist(std::vector<std::filesystem::path> files);
    void add(const std::filesystem::path& file);
    void remove(int index);
    std::vector<MusicTrackInfo> playlist() const;
    void play(int index);      // starts that track (fade-in 50 ms)
    void pause();              // 20 ms ramp, keeps the position
    void resume();
    void next();
    void fadeOut(float ms);    // fade, then pause
    void stop();
    AtomicParam& volume() noexcept { return volume_; }
    std::atomic<bool>& autoAdvance() noexcept { return autoAdvance_; }

    struct Status
    {
        bool playing = false, paused = false;
        int index = -1;
        double position = 0.0, duration = 0.0;
        uint64_t underruns = 0;
    };
    Status status() const;

    // ----- audio thread -----
    // Renders the music into L/R (overwrites). `duckGain` per sample (nullptr = unity).
    void render(float* left, float* right, int frames, const float* duckGain) noexcept;

private:
    void decoderMain();

    int rate_;
    mutable std::mutex mutex_;
    std::vector<MusicTrackInfo> playlist_;
    std::thread decoder_;
    std::atomic<bool> quit_{false};

    // Requests (control → decoder/audio).
    std::atomic<int> requestedIndex_{-1};
    std::atomic<uint32_t> startGen_{0};  // bump to (re)start requestedIndex_
    std::atomic<bool> paused_{false};
    std::atomic<bool> stopped_{true};
    std::atomic<float> fadeMs_{0.0f};
    std::atomic<uint32_t> fadeGen_{0};
    AtomicParam volume_{0.8f};
    std::atomic<bool> autoAdvance_{true};

    // Decoder → audio thread.
    SpscRing<float> ring_;               // interleaved stereo at the engine rate
    std::atomic<uint32_t> flushRequest_{0}, flushAck_{0};
    std::atomic<bool> endOfTrack_{false};
    std::atomic<int> playingIndex_{-1};
    std::atomic<double> duration_{0.0};

    // Audio-thread state.
    float gain_ = 0.0f, fade_ = 1.0f, fadeStep_ = 0.0f;
    uint32_t seenFade_ = 0;
    std::atomic<uint64_t> framesPlayed_{0}, underruns_{0};
    std::atomic<bool> audible_{false};
    std::atomic<bool> switching_{false}; // a track is opening: an empty ring is expected, not an underrun
};

} // namespace pf8
