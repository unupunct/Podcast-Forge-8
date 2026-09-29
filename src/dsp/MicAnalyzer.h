#pragma once
// Microphone setup analysis (DSP.md §4). Fed with the raw channel input (before trim) on the UI
// thread; measures the noise floor during a silence phase and speech level during a talk phase,
// then recommends software trim and dynamics settings. It never touches hardware gain.
#include <string>
#include <vector>

#include "dsp/Biquad.h"

namespace pf8::dsp {

struct MicAnalysis
{
    bool haveNoise = false, haveSpeech = false;
    float noiseFloorDb = -120.0f;   // A-weighted, 10th percentile of 100 ms windows
    float noiseLowBiasDb = 0.0f;    // unweighted minus A-weighted noise: rumble/hum indicator
    float peakDb = -120.0f;         // speech phase, sample peak
    float averageDb = -120.0f;      // speech phase, RMS of speech-gated windows
    int clipCount = 0;              // samples ≥ −0.1 dBFS
    bool signalDetected = false;

    // Recommendations
    float recommendedTrimDb = 0.0f;
    float hpfHz = 80.0f;
    float gateThresholdDb = -50.0f;
    float compThresholdDb = -20.0f; // post-trim level
    float limiterCeilingDb = -1.0f;
    std::vector<std::string> advice;
};

class MicAnalyzer
{
public:
    enum class Phase { Idle, Noise, Speech };

    void prepare(double fs);
    void begin(Phase phase);         // starts collecting for a phase (clears that phase's data)
    void feed(const float* x, int n);
    MicAnalysis result() const;      // computed from whatever has been collected
    Phase phase() const noexcept { return phase_; }
    double collectedSeconds() const noexcept;
    float lastWindowDb() const noexcept { return lastWindowDb_; } // live meter

private:
    void closeWindow();

    double fs_ = 48000;
    Phase phase_ = Phase::Idle;
    AWeighting aw_;
    int windowLen_ = 4800, inWindow_ = 0;
    double sumSq_ = 0.0, sumSqA_ = 0.0;
    float windowPeak_ = 0.0f, lastWindowDb_ = -120.0f;
    std::vector<float> noiseA_, noiseRaw_, speechRms_;
    float speechPeak_ = 0.0f;
    int clips_ = 0;
    size_t speechSamples_ = 0, noiseSamples_ = 0;
};

} // namespace pf8::dsp
