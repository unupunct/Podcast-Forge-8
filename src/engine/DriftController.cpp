#include "engine/DriftController.h"

#include <cmath>

namespace pf8 {

const char* toString(SyncStatus s) noexcept
{
    switch (s)
    {
        case SyncStatus::Offline:    return "Offline";
        case SyncStatus::Priming:    return "Priming";
        case SyncStatus::Converging: return "Converging";
        case SyncStatus::Locked:     return "Locked";
        case SyncStatus::Unstable:   return "Unstable";
        case SyncStatus::Native:     return "Native";
    }
    return "?";
}

void DriftController::prepare(double engineRate, double targetFill, double lockToleranceFrames)
{
    rate_ = engineRate;
    target_ = targetFill;
    lockTol_ = lockToleranceFrames;
    const double wn = tuning().wn, zeta = tuning().zeta;
    tau_ = tuning().filterTau;
    kp_ = 2.0 * zeta * wn / rate_;
    ki_ = wn * wn / rate_;
    reset(0.0);
}

void DriftController::reset(double keepPpm) noexcept
{
    ppm_ = keepPpm;
    integral_ = ki_ > 0 ? (keepPpm * 1e-6) / ki_ : 0.0;
    avgFill_ = target_;
    for (double& s : stages_) s = target_;
    lockedSeconds_ = 0.0;
    clampedSeconds_ = 0.0;
    first_ = true;
    status_ = SyncStatus::Converging;
}

double DriftController::update(double fillFrames, int blockFrames) noexcept
{
    const double dt = blockFrames / rate_;
    // The filter starts at the target (reset): bridges prime centred on it, whereas the first raw
    // sample can sit up to a device period away from the mean and would kick the loop into the clamp.
    first_ = false;
    // Three cascaded one-pole filters (τ = 0.1 s each): device/engine block beats (tens of Hz)
    // are attenuated by > 60 dB, while adding only ~0.3 s of lag to the loop (ωn = 0.2 rad/s).
    const double alpha = dt / (tau_ + dt);
    double x = fillFrames;
    for (double& s : stages_)
    {
        s += alpha * (x - s);
        x = s;
    }
    avgFill_ = x;
    const double e = avgFill_ - target_;

    double c = kp_ * e + ki_ * (integral_ + e * dt);
    const double maxC = kMaxPpm * 1e-6;
    const bool clamped = std::abs(c) > maxC;
    if (clamped)
        c = c > 0 ? maxC : -maxC;
    else
        integral_ += e * dt; // anti-windup: integrate only when not saturated
    ppm_ = c * 1e6;

    clampedSeconds_ = clamped ? clampedSeconds_ + dt : 0.0;
    lockedSeconds_ = std::abs(e) < lockTol_ ? lockedSeconds_ + dt : 0.0;
    if (clampedSeconds_ > 2.0)
        status_ = SyncStatus::Unstable;
    else if (lockedSeconds_ > 10.0)
        status_ = SyncStatus::Locked;
    else if (status_ != SyncStatus::Locked || std::abs(e) > 4.0 * lockTol_)
        status_ = SyncStatus::Converging;
    return ppm_;
}

} // namespace pf8
