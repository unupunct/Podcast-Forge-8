#pragma once
// Polyphase windowed-sinc resampler with a continuously variable ratio.
//
// ratio = input rate / output rate. The nominal ratio is fixed at prepare(); a small correction
// (in ppm, from the drift controller) can change every call and is slew-limited so it never
// produces a step. With nominal ratio 1 and zero correction the resampler is an exact passthrough
// with zero latency (used for the master device).
//
// Real-time safe after prepare(): no allocation, no locks.
#include <vector>

namespace pf8 {

class VarResampler
{
public:
    static constexpr int kTaps = 48;
    static constexpr int kPhases = 256;
    static constexpr int kLatency = kTaps / 2; // input frames of delay (non-passthrough)

    // maxOutFrames: the largest outFrames any single process() call will ask for.
    // allowPassthrough: exact zero-latency copy when nominalRatio == 1 (master bridges only). In
    // passthrough mode ratio corrections are ignored.
    void prepare(int channels, double nominalRatio, int maxOutFrames, bool allowPassthrough = false);

    void setRatioCorrectionPpm(double ppm) noexcept { if (!passthrough_) targetPpm_ = ppm; }
    double ratioCorrectionPpm() const noexcept { return appliedPpm_; }
    double effectiveRatio() const noexcept { return nominal_ * (1.0 + appliedPpm_ * 1e-6); }
    bool passthrough() const noexcept { return passthrough_; }

    // Input frames that must be pushed before process(outFrames) can produce outFrames frames.
    int inputFramesNeeded(int outFrames) const noexcept;
    // Largest input chunk pushInput accepts in one go (buffer headroom).
    int maxInputFrames() const noexcept { return capacityFrames_ - buffered_; }

    // Returns frames accepted (all of them unless the internal buffer is full).
    int pushInput(const float* interleaved, int frames) noexcept;

    // Produces up to outFrames interleaved frames; returns the number produced (== outFrames when
    // enough input was pushed).
    int process(float* interleavedOut, int outFrames) noexcept;

    int bufferedInput() const noexcept { return buffered_ - static_cast<int>(pos_); }
    int channels() const noexcept { return channels_; }
    void reset() noexcept;

private:
    double nextRatio(int outFrames) const noexcept;
    void compact() noexcept;

    int channels_ = 1;
    double nominal_ = 1.0;
    bool passthrough_ = false;
    double targetPpm_ = 0.0;
    double appliedPpm_ = 0.0;
    std::vector<float> table_;   // (kPhases + 1) × kTaps
    std::vector<float> buffer_;  // interleaved input history + pending input
    int capacityFrames_ = 0;
    int buffered_ = 0;           // frames in buffer_
    double pos_ = 0.0;           // centre of the next output, in buffer frames
};

} // namespace pf8
