#pragma once
// Gate, compressor, de-esser, look-ahead limiter. All real-time safe after prepare().
#include <vector>

#include "dsp/Biquad.h"
#include "dsp/DspParams.h"

namespace pf8::dsp {

inline float dbToLin(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
inline float linToDb(float x) noexcept { return x > 1e-9f ? 20.0f * std::log10(x) : -180.0f; }
// One-pole smoothing coefficient for a time constant in milliseconds.
inline float timeCoeff(double fs, double ms) noexcept
{
    return ms <= 0.0 ? 0.0f : static_cast<float>(std::exp(-1.0 / (fs * ms * 0.001)));
}

struct GateSettings
{
    float thresholdDb = -50, rangeDb = -40, attackMs = 2, holdMs = 80, releaseMs = 150;
};

class NoiseGate
{
public:
    void prepare(double fs);
    void reset();
    void set(const GateSettings& s) noexcept { s_ = s; }
    void process(float* x, int n) noexcept;
    float gain() const noexcept { return gain_; } // current linear gain (1 = open)
    bool open() const noexcept { return open_; }

private:
    double fs_ = 48000;
    GateSettings s_;
    Biquad sidechainHp_; // keeps rumble from opening the gate
    float env_ = 0.0f, gain_ = 1.0f;
    int holdLeft_ = 0;
    bool open_ = true;
};

struct CompressorSettings
{
    float thresholdDb = -20, ratio = 4, attackMs = 5, releaseMs = 100, kneeDb = 6, makeupDb = 6;
    bool autoMakeup = false;
    DetectorMode detector = DetectorMode::Rms;
};

class Compressor
{
public:
    void prepare(double fs);
    void reset();
    void set(const CompressorSettings& s) noexcept { s_ = s; }
    void process(float* x, int n) noexcept;
    float gainReductionDb() const noexcept { return grDb_; } // ≤ 0
    // Static curve: gain change in dB for an input level in dB (≤ 0), without makeup.
    static float staticGainDb(float inDb, float thresholdDb, float ratio, float kneeDb) noexcept;
    float makeupDb() const noexcept;

private:
    double fs_ = 48000;
    CompressorSettings s_;
    float rms_ = 0.0f, peak_ = 0.0f, grDb_ = 0.0f;
};

struct DeEsserSettings
{
    float freq = 6500, thresholdDb = -30, amountDb = 6;
};

class DeEsser
{
public:
    void prepare(double fs);
    void reset();
    void set(const DeEsserSettings& s) noexcept { s_ = s; }
    void process(float* x, int n) noexcept;
    float reductionDb() const noexcept { return reductionDb_; } // ≥ 0

private:
    double fs_ = 48000;
    DeEsserSettings s_;
    Biquad detector_, cut_;
    float env_ = 0.0f, reductionDb_ = 0.0f, appliedDb_ = 0.0f, lastFreq_ = 0.0f;
};

// Brick-wall look-ahead limiter, linked across `channels`. The output never exceeds the ceiling:
// the gain for each sample is ≤ the minimum requirement over its look-ahead window (sliding
// minimum, then a box average of that minimum, then release smoothing that only ever raises the
// gain toward the allowed value), plus a final safety clamp.
class Limiter
{
public:
    void prepare(double fs, int channels, int maxBlock, double lookaheadMs = 1.5, double releaseMs = 50.0);
    void reset();
    void setCeilingDb(float db) noexcept { ceiling_ = dbToLin(db); }
    void setTruePeak(bool on) noexcept { truePeak_ = on; }
    // In-place; x[c][i]. Delays the signal by latency() samples (also when bypassed, see below).
    void process(float* const* x, int n, bool enabled) noexcept;
    int latency() const noexcept { return lookahead_; }
    float gainReductionDb() const noexcept { return linToDb(lastGain_); }

private:
    float requirement(int i, float* const* x) noexcept;

    int channels_ = 1, lookahead_ = 72;
    float ceiling_ = 0.891f, release_ = 0.999f;
    bool truePeak_ = false;
    std::vector<float> delay_;        // [channel][lookahead+1] ring
    int delayPos_ = 0;
    // Sliding minimum of the gain requirement over the last L+1 samples: a monotonic deque of
    // (absolute sample index, value) in a fixed ring.
    std::vector<long long> qIdx_;
    std::vector<float> qVal_;
    int qHead_ = 0, qCount_ = 0, qCap_ = 0;
    std::vector<float> avgRing_;
    double avgSum_ = 0.0;
    int avgPos_ = 0;
    long long counter_ = 0;
    float gain_ = 1.0f, lastGain_ = 1.0f;
    float tpHist_[2][4]{};            // last samples per channel for the true-peak interpolator
};

} // namespace pf8::dsp
