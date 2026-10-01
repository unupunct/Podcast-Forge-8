#pragma once
// Second-order delay-locked loop that turns jittery callback times into a smooth, present-anchored
// timeline that follows the device's true sample rate (after F. Adriaensen, "Using a DLL to filter
// time", LAC 2005), generalised to callbacks of varying frame counts.
//
// Used on both sides of every bridge: the master's DLL is the engine's "now", each device's DLL is
// that device's timeline. Raw WASAPI packet timestamps are not used — on real devices they wobble
// by several milliseconds, which is ~50 frames of false fill error per millisecond.
#include <cmath>
#include <cstdint>

namespace pf8 {

class TimeDll
{
public:
    // nominalFramesPerCallback / sampleRate define the loop's update interval for its coefficients.
    void reset(double sampleRate, double nominalFramesPerCallback, double bandwidthHz = 0.5) noexcept
    {
        nsPerFrame_ = 1e9 / sampleRate;
        nominalFrames_ = nominalFramesPerCallback;
        const double T = nominalFramesPerCallback / sampleRate;
        const double omega = 2.0 * 3.14159265358979323846 * bandwidthHz * T;
        b_ = std::sqrt(2.0) * omega;
        c_ = omega * omega;
        started_ = false;
    }

    // A callback at `nowNs` that delivered/consumed `frames`. Returns the filtered time of this
    // callback.
    int64_t update(int64_t nowNs, int frames) noexcept
    {
        const double now = static_cast<double>(nowNs);
        if (!started_)
        {
            t_ = now;
            started_ = true;
            return nowNs;
        }
        const double predicted = t_ + nsPerFrame_ * frames;
        const double e = now - predicted;
        // A gap far beyond a callback (stall, device hiccup): re-anchor instead of slewing.
        if (std::abs(e) > 20.0 * nsPerFrame_ * nominalFrames_)
        {
            t_ = now;
            return nowNs;
        }
        // Adriaensen's loop with the period expressed per frame: t1 += b·e + period, period += c·e.
        t_ = predicted + b_ * e;
        nsPerFrame_ += c_ * e / nominalFrames_;
        return static_cast<int64_t>(t_);
    }

    double nsPerFrame() const noexcept { return nsPerFrame_; }
    bool started() const noexcept { return started_; }

private:
    double nsPerFrame_ = 1e9 / 48000.0, nominalFrames_ = 480.0;
    double b_ = 0.0, c_ = 0.0, t_ = 0.0;
    bool started_ = false;
};

} // namespace pf8
