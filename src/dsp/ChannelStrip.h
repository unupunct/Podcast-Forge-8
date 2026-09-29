#pragma once
// One channel's processing chain (DSP.md §1):
//   trim → polarity → HPF → gate → EQ4 → de-esser → compressor → limiter
// Every stage bypasses with a 10 ms crossfade; the limiter's 1.5 ms look-ahead is always present
// (also when off) so every channel has the same constant latency and tracks stay aligned.
#include <array>
#include <vector>

#include "dsp/Biquad.h"
#include "dsp/DspParams.h"
#include "dsp/Dynamics.h"

namespace pf8::dsp {

struct StripMeters
{
    float gateGain = 1.0f;     // linear, 1 = open
    float compGrDb = 0.0f;     // ≤ 0
    float deessDb = 0.0f;      // ≥ 0 reduction
    float limiterGrDb = 0.0f;  // ≤ 0
};

class ChannelStrip
{
public:
    void prepare(double fs, int maxBlock);
    void reset();
    void process(float* x, int n, const ChannelDspParams& p) noexcept;
    int latency() const noexcept { return limiter_.latency(); }
    StripMeters meters() const noexcept { return meters_; }

private:
    // A bypassable stage: processes a copy and crossfades wet/dry.
    struct Blend
    {
        float mix = 0.0f, target = 0.0f, step = 0.0f;
        bool active() const noexcept { return mix > 0.0f || target > 0.0f; }
    };
    void setTarget(Blend& b, bool on) noexcept;
    template <typename F>
    void runStage(Blend& b, float* x, int n, F&& fn) noexcept;

    double fs_ = 48000;
    int maxBlock_ = 512;
    float trimGain_ = 1.0f, polarity_ = 1.0f;
    Biquad hpf_[2];
    float hpfHz_ = -1.0f;
    bool hpf24_ = false;
    std::array<Biquad, 4> eq_;
    std::array<BiquadCoeffs, 4> eqCoeffs_;
    struct EqDesign { float f = -1, g = 0, q = 0; EqBandType t = EqBandType::Peak; bool valid = false; };
    std::array<EqDesign, 4> eqDesign_{};
    NoiseGate gate_;
    DeEsser deesser_;
    Compressor comp_;
    Limiter limiter_;
    Blend bHpf_, bGate_, bEq_, bDeess_, bComp_;
    std::vector<float> wet_;
    StripMeters meters_;
    float fadeStep_ = 0.0f;
};

} // namespace pf8::dsp
