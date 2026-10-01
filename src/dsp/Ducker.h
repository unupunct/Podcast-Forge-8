#pragma once
// Automatic ducking (DSP.md §5): music goes down while people talk, and returns gently after.
// Sidechain = the voices on the program bus. Real-time safe.
#include <cmath>

#include "core/AtomicParam.h"

namespace pf8::dsp {

struct DuckerParams
{
    std::atomic<bool> on{true};
    AtomicParam thresholdDb{-35.0f}; // speech level that triggers ducking
    AtomicParam depthDb{-15.0f};     // music attenuation while ducked
    AtomicParam attackMs{50.0f};
    AtomicParam holdMs{500.0f};
    AtomicParam releaseMs{1500.0f};
};

class Ducker
{
public:
    void prepare(double fs)
    {
        fs_ = fs;
        rms_ = 0.0;
        gain_ = 1.0f;
        holdLeft_ = 0;
        aboveFor_ = 0;
    }

    // `voice`: sidechain samples for this block (mono sum of the voices). Writes the music gain
    // for every sample into `gainOut` and returns the last value.
    float process(const float* voice, int n, const DuckerParams& p, float* gainOut) noexcept
    {
        const bool on = p.on.load(std::memory_order_relaxed);
        const double rmsCoef = std::exp(-1.0 / (fs_ * 0.020));
        const float thrOn = std::pow(10.0f, p.thresholdDb.get() / 20.0f);
        const float thrOff = std::pow(10.0f, (p.thresholdDb.get() - 3.0f) / 20.0f); // 3 dB hysteresis
        const float duckGain = std::pow(10.0f, std::min(0.0f, p.depthDb.get()) / 20.0f);
        const int attackSamples = std::max(1, static_cast<int>(fs_ * std::max(1.0f, p.attackMs.get()) * 0.001));
        const int hold = static_cast<int>(fs_ * std::max(0.0f, p.holdMs.get()) * 0.001);
        const float attStep = (1.0f - duckGain) / static_cast<float>(attackSamples);
        const float relStep = (1.0f - duckGain) / static_cast<float>(std::max(1.0, fs_ * std::max(10.0f, p.releaseMs.get()) * 0.001));
        for (int i = 0; i < n; ++i)
        {
            const double v = voice ? voice[i] : 0.0;
            rms_ = rmsCoef * rms_ + (1.0 - rmsCoef) * v * v;
            const float level = static_cast<float>(std::sqrt(rms_));
            // Ducking starts once the voice has stayed above threshold for the attack time,
            // and the gain then reaches the depth over the attack time.
            if (level >= thrOn) aboveFor_ = std::min(aboveFor_ + 1, attackSamples * 4);
            else if (level < thrOff) aboveFor_ = 0;
            const bool speaking = on && aboveFor_ >= attackSamples; // above threshold for the attack time
            if (speaking) holdLeft_ = hold;
            else if (holdLeft_ > 0) --holdLeft_;
            const bool ducked = on && (speaking || holdLeft_ > 0);
            if (ducked) gain_ = std::max(duckGain, gain_ - attStep);
            else gain_ = std::min(1.0f, gain_ + relStep);
            if (gainOut) gainOut[i] = gain_;
        }
        return gain_;
    }

    float gain() const noexcept { return gain_; }

private:
    double fs_ = 48000.0, rms_ = 0.0;
    float gain_ = 1.0f;
    int holdLeft_ = 0, aboveFor_ = 0;
};

} // namespace pf8::dsp
