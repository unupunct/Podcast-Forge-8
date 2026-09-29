#pragma once
// PI controller that holds a bridge's ring fill at its target by steering the resampler ratio.
//
// Loop design (AUDIO_ENGINE.md): fill error e (frames) is averaged by three cascaded 0.2 s one-pole filters;
// correction c = Kp·e + Ki·∫e dt (as a fraction, reported in ppm). With the fill integrator
// de/dt = R(δ − c) this gives s² + R·Kp·s + R·Ki with ωn = 0.2 rad/s, ζ = 1: no overshoot, ~20 s
// settling, and 100 Hz callback ripple attenuated > 300×.
#include <cstdint>

namespace pf8 {

enum class SyncStatus : uint8_t { Offline, Priming, Converging, Locked, Unstable, Native };
const char* toString(SyncStatus s) noexcept;

class DriftController
{
public:
    static constexpr double kMaxPpm = 1000.0;

    void prepare(double engineRate, double targetFill, double lockToleranceFrames);
    // Call once per engine block with the measured fill (frames). Returns the ppm correction.
    double update(double fillFrames, int blockFrames) noexcept;
    void reset(double keepPpm) noexcept;

    double ppm() const noexcept { return ppm_; }
    double averagedFill() const noexcept { return avgFill_; }
    double target() const noexcept { return target_; }
    SyncStatus status() const noexcept { return status_; }

private:
    double rate_ = 48000.0;
    double target_ = 0.0;
    double lockTol_ = 64.0;
    double kp_ = 0.0, ki_ = 0.0;
    double avgFill_ = 0.0;
    double stages_[3]{};
    double integral_ = 0.0; // ∫ e dt  (frame·seconds)
    double ppm_ = 0.0;
    double lockedSeconds_ = 0.0;
    double clampedSeconds_ = 0.0;
    bool first_ = true;
    SyncStatus status_ = SyncStatus::Converging;
};

} // namespace pf8
