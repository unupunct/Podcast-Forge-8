#pragma once
// Soundboard: 24 carts (brief §14). The audio thread plays pre-decoded, immutable buffers; the
// control/UI side loads files and sends play/stop/fade requests through atomics.
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/AtomicParam.h"

namespace pf8 {

constexpr int kCartCount = 24;

// Decoded audio at the engine rate, interleaved stereo. Never modified after creation.
struct CartBuffer
{
    std::vector<float> samples; // L R L R …
    int64_t frames = 0;
    std::string sourcePath;
    std::string error; // non-empty when loading failed
};

struct CartSettings
{
    std::string name;
    std::string colour = "#3fa9f5";
    std::string hotkey;      // display/binding string (bound by the hotkey manager)
    AtomicParam volume{1.0f}; // linear
    AtomicParam fadeInMs{0.0f};
    AtomicParam fadeOutMs{1500.0f};
    std::atomic<bool> loop{false};
};

struct CartState // published by the audio thread
{
    std::atomic<bool> playing{false};
    std::atomic<int64_t> position{0};
    std::atomic<int64_t> length{0};
};

class Soundboard
{
public:
    explicit Soundboard(int sampleRate);

    // ----- control / UI thread -----
    void setSampleRate(int rate) noexcept { rate_ = rate; } // before playback starts
    int sampleRate() const noexcept { return rate_; }
    void setBuffer(int cart, std::shared_ptr<const CartBuffer> buffer); // nullptr clears
    std::shared_ptr<const CartBuffer> buffer(int cart) const;
    CartSettings& settings(int cart) noexcept { return settings_[static_cast<size_t>(cart)]; }
    const CartState& state(int cart) const noexcept { return state_[static_cast<size_t>(cart)]; }
    void play(int cart) noexcept;      // starts, or restarts from the beginning
    void stop(int cart) noexcept;      // 5 ms ramp, then stops
    void fadeOut(int cart) noexcept;   // over the cart's fade-out time
    void stopAll() noexcept;
    void fadeAll() noexcept;
    void collectGarbage();             // frees buffers the audio thread no longer uses

    // ----- audio thread -----
    // Renders the sum of all carts into L/R (overwrites). Real-time safe.
    void render(float* left, float* right, int frames) noexcept;

private:
    struct Voice // audio-thread only
    {
        const CartBuffer* buf = nullptr;
        int64_t pos = 0;
        float gain = 0.0f;      // envelope
        float step = 0.0f;      // per-sample envelope delta
        float target = 0.0f;
        bool active = false;
        bool stopAtTarget = false;
        uint32_t seenPlay = 0, seenStop = 0, seenFade = 0;
    };

    int rate_;
    std::array<CartSettings, kCartCount> settings_;
    std::array<CartState, kCartCount> state_;
    std::array<std::atomic<const CartBuffer*>, kCartCount> active_{};  // requested buffer
    std::array<std::atomic<const CartBuffer*>, kCartCount> playing_{}; // published by the audio thread
    std::array<std::atomic<uint32_t>, kCartCount> playReq_{}, stopReq_{}, fadeReq_{};
    std::array<Voice, kCartCount> voices_{};

    mutable std::mutex ownersMutex_;
    std::array<std::shared_ptr<const CartBuffer>, kCartCount> owners_;
    // A retired buffer is freed only after a render that started after its retirement has finished
    // (renders_ counts completed renders) and no voice still publishes it as playing.
    std::vector<std::pair<std::shared_ptr<const CartBuffer>, uint64_t>> retired_;
    std::atomic<uint64_t> renders_{0};
};

} // namespace pf8
