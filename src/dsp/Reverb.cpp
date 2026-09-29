#include "dsp/Reverb.h"

#include <algorithm>

namespace pf8::dsp {
namespace {
// Freeverb tunings at 44.1 kHz; scaled to the actual rate. Right side is offset by 23 samples.
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllPassTuning[4] = {556, 441, 341, 225};
constexpr int kStereoSpread = 23;
constexpr float kInputGain = 0.015f;
} // namespace

void Reverb::prepare(double fs)
{
    const double scale = fs / 44100.0;
    for (size_t i = 0; i < 8; ++i)
    {
        combL_[i].buf.assign(static_cast<size_t>(kCombTuning[i] * scale), 0.0f);
        combR_[i].buf.assign(static_cast<size_t>((kCombTuning[i] + kStereoSpread) * scale), 0.0f);
    }
    for (size_t i = 0; i < 4; ++i)
    {
        apL_[i].buf.assign(static_cast<size_t>(kAllPassTuning[i] * scale), 0.0f);
        apR_[i].buf.assign(static_cast<size_t>((kAllPassTuning[i] + kStereoSpread) * scale), 0.0f);
    }
    reset();
    setParameters(0.45f, 0.5f, 1.0f);
}

void Reverb::reset()
{
    for (auto* arr : {&combL_, &combR_})
        for (auto& c : *arr)
        {
            std::fill(c.buf.begin(), c.buf.end(), 0.0f);
            c.pos = 0;
            c.store = 0.0f;
        }
    for (auto* arr : {&apL_, &apR_})
        for (auto& a : *arr)
        {
            std::fill(a.buf.begin(), a.buf.end(), 0.0f);
            a.pos = 0;
        }
}

void Reverb::setParameters(float roomSize, float damping, float width) noexcept
{
    feedback_ = 0.7f + std::clamp(roomSize, 0.0f, 1.0f) * 0.28f;
    damp1_ = std::clamp(damping, 0.0f, 1.0f) * 0.4f;
    damp2_ = 1.0f - damp1_;
    const float w = std::clamp(width, 0.0f, 1.0f);
    wet1_ = 0.5f * (1.0f + w);
    wet2_ = 0.5f * (1.0f - w);
}

void Reverb::processAdd(const float* in, float* outL, float* outR, int n, float wet) noexcept
{
    if (wet <= 0.0f) return;
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i] * kInputGain;
        float l = 0.0f, r = 0.0f;
        for (size_t c = 0; c < 8; ++c)
        {
            l += combL_[c].process(x, feedback_, damp1_, damp2_);
            r += combR_[c].process(x, feedback_, damp1_, damp2_);
        }
        for (size_t a = 0; a < 4; ++a)
        {
            l = apL_[a].process(l);
            r = apR_[a].process(r);
        }
        outL[i] += wet * (l * wet1_ + r * wet2_);
        outR[i] += wet * (r * wet1_ + l * wet2_);
    }
}

} // namespace pf8::dsp
