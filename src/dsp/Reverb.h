#pragma once
// Shared room reverb (Freeverb topology — Jezar's public-domain design: 8 damped comb filters per
// side + 4 series all-passes). Mono in, stereo out. Real-time safe after prepare().
#include <array>
#include <vector>

namespace pf8::dsp {

class Reverb
{
public:
    void prepare(double fs);
    void reset();
    void setParameters(float roomSize, float damping, float width) noexcept;
    // Adds the wet signal of `in` into outL/outR scaled by `wet`.
    void processAdd(const float* in, float* outL, float* outR, int n, float wet) noexcept;

private:
    struct Comb
    {
        std::vector<float> buf;
        int pos = 0;
        float store = 0.0f;
        float process(float x, float feedback, float damp1, float damp2) noexcept
        {
            const float out = buf[static_cast<size_t>(pos)];
            store = out * damp2 + store * damp1;
            buf[static_cast<size_t>(pos)] = x + store * feedback;
            if (++pos >= static_cast<int>(buf.size())) pos = 0;
            return out;
        }
    };
    struct AllPass
    {
        std::vector<float> buf;
        int pos = 0;
        float process(float x) noexcept
        {
            const float b = buf[static_cast<size_t>(pos)];
            const float out = b - x;
            buf[static_cast<size_t>(pos)] = x + b * 0.5f;
            if (++pos >= static_cast<int>(buf.size())) pos = 0;
            return out;
        }
    };

    std::array<Comb, 8> combL_, combR_;
    std::array<AllPass, 4> apL_, apR_;
    float feedback_ = 0.84f, damp1_ = 0.2f, damp2_ = 0.8f, wet1_ = 1.0f, wet2_ = 0.0f;
};

} // namespace pf8::dsp
