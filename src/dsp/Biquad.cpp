#include "dsp/Biquad.h"

#include <algorithm>
#include <complex>

namespace pf8::dsp {
namespace {

BiquadCoeffs normalise(double b0, double b1, double b2, double a0, double a1, double a2)
{
    BiquadCoeffs c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

double clampFreq(double fs, double f) { return std::clamp(f, 1.0, fs * 0.49); }

} // namespace

BiquadCoeffs BiquadCoeffs::highPass(double fs, double f, double q)
{
    const double w = 2 * kPi * clampFreq(fs, f) / fs, cw = std::cos(w), al = std::sin(w) / (2 * q);
    return normalise((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
}

BiquadCoeffs BiquadCoeffs::lowPass(double fs, double f, double q)
{
    const double w = 2 * kPi * clampFreq(fs, f) / fs, cw = std::cos(w), al = std::sin(w) / (2 * q);
    return normalise((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
}

BiquadCoeffs BiquadCoeffs::bandPass(double fs, double f, double q)
{
    const double w = 2 * kPi * clampFreq(fs, f) / fs, cw = std::cos(w), al = std::sin(w) / (2 * q);
    return normalise(al, 0, -al, 1 + al, -2 * cw, 1 - al);
}

BiquadCoeffs BiquadCoeffs::peak(double fs, double f, double q, double gainDb)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * kPi * clampFreq(fs, f) / fs, cw = std::cos(w), al = std::sin(w) / (2 * q);
    return normalise(1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
}

BiquadCoeffs BiquadCoeffs::lowShelf(double fs, double f, double q, double gainDb)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * kPi * clampFreq(fs, f) / fs, cw = std::cos(w), al = std::sin(w) / (2 * q);
    const double sA = 2 * std::sqrt(A) * al;
    return normalise(A * ((A + 1) - (A - 1) * cw + sA), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sA),
                     (A + 1) + (A - 1) * cw + sA, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sA);
}

BiquadCoeffs BiquadCoeffs::highShelf(double fs, double f, double q, double gainDb)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * kPi * clampFreq(fs, f) / fs, cw = std::cos(w), al = std::sin(w) / (2 * q);
    const double sA = 2 * std::sqrt(A) * al;
    return normalise(A * ((A + 1) + (A - 1) * cw + sA), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sA),
                     (A + 1) - (A - 1) * cw + sA, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sA);
}

BiquadCoeffs BiquadCoeffs::firstOrderHighPass(double fs, double f)
{
    // Bilinear transform of s / (s + wc) with pre-warping.
    const double k = std::tan(kPi * clampFreq(fs, f) / fs);
    return normalise(1, -1, 0, 1 + k, k - 1, 0);
}

BiquadCoeffs BiquadCoeffs::firstOrderLowPass(double fs, double f)
{
    const double k = std::tan(kPi * clampFreq(fs, f) / fs);
    return normalise(k, k, 0, 1 + k, k - 1, 0);
}

double BiquadCoeffs::magnitude(double fs, double f) const
{
    const std::complex<double> z = std::polar(1.0, -2 * kPi * f / fs);
    const auto num = b0 + b1 * z + b2 * z * z;
    const auto den = 1.0 + a1 * z + a2 * z * z;
    return std::abs(num / den);
}

void Biquad::processInterpolated(float* x, int n, const BiquadCoeffs& t) noexcept
{
    if (n <= 0) return;
    const BiquadCoeffs s = c_;
    const double inv = 1.0 / n;
    for (int i = 0; i < n; ++i)
    {
        const double a = (i + 1) * inv;
        c_.b0 = s.b0 + (t.b0 - s.b0) * a;
        c_.b1 = s.b1 + (t.b1 - s.b1) * a;
        c_.b2 = s.b2 + (t.b2 - s.b2) * a;
        c_.a1 = s.a1 + (t.a1 - s.a1) * a;
        c_.a2 = s.a2 + (t.a2 - s.a2) * a;
        x[i] = process(x[i]);
    }
    c_ = t;
}

void AWeighting::prepare(double fs)
{
    // Poles of the analogue A-weighting curve: 20.6 Hz (×2), 107.7 Hz, 737.9 Hz, 12194 Hz (×2).
    s_[0].setCoeffs(BiquadCoeffs::firstOrderHighPass(fs, 20.598997));
    s_[1].setCoeffs(BiquadCoeffs::firstOrderHighPass(fs, 20.598997));
    s_[2].setCoeffs(BiquadCoeffs::firstOrderHighPass(fs, 107.65265));
    s_[3].setCoeffs(BiquadCoeffs::firstOrderHighPass(fs, 737.86223));
    s_[4].setCoeffs(BiquadCoeffs::firstOrderLowPass(fs, 12194.217));
    s_[5].setCoeffs(BiquadCoeffs::firstOrderLowPass(fs, 12194.217));
    double m = 1.0;
    for (auto& s : s_) m *= s.coeffs().magnitude(fs, 1000.0);
    gain_ = 1.0 / m;
    reset();
}

void AWeighting::reset() noexcept
{
    for (auto& s : s_) s.reset();
}

float AWeighting::process(float x) noexcept
{
    float y = static_cast<float>(x * gain_);
    for (auto& s : s_) y = s.process(y);
    return y;
}

} // namespace pf8::dsp
