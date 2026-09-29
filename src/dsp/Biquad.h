#pragma once
// RBJ-cookbook biquads (transposed direct form II), coefficient design, and the A-weighting
// filter used by the mic wizard. Real-time safe.
#include <cmath>

namespace pf8::dsp {

constexpr double kPi = 3.14159265358979323846;

struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // a0 normalised to 1

    static BiquadCoeffs highPass(double fs, double f, double q);
    static BiquadCoeffs lowPass(double fs, double f, double q);
    static BiquadCoeffs bandPass(double fs, double f, double q); // 0 dB peak gain
    static BiquadCoeffs peak(double fs, double f, double q, double gainDb);
    static BiquadCoeffs lowShelf(double fs, double f, double q, double gainDb);
    static BiquadCoeffs highShelf(double fs, double f, double q, double gainDb);
    static BiquadCoeffs firstOrderHighPass(double fs, double f);
    static BiquadCoeffs firstOrderLowPass(double fs, double f);

    // |H(e^jw)| at frequency f (for tests and displays).
    double magnitude(double fs, double f) const;
};

class Biquad
{
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
    const BiquadCoeffs& coeffs() const noexcept { return c_; }
    void reset() noexcept { z1_ = z2_ = 0.0; }

    float process(float x) noexcept
    {
        const double y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return static_cast<float>(y);
    }

    void process(float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i) x[i] = process(x[i]);
    }

    // Moves coefficients linearly from the current set to `target` across n samples (no zipper).
    void processInterpolated(float* x, int n, const BiquadCoeffs& target) noexcept;

private:
    BiquadCoeffs c_;
    double z1_ = 0.0, z2_ = 0.0;
};

// IEC 61672 A-weighting as a cascade of first-order sections, normalised to 0 dB at 1 kHz.
class AWeighting
{
public:
    void prepare(double fs);
    void reset() noexcept;
    float process(float x) noexcept;

private:
    Biquad s_[6];
    double gain_ = 1.0;
};

} // namespace pf8::dsp
