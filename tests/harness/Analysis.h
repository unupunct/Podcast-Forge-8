#pragma once
// Signal generators and analysis helpers for engine tests.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pf8test {

constexpr double kPi = 3.14159265358979323846;

inline double db(double linear) { return 20.0 * std::log10(std::max(linear, 1e-12)); }

struct Sine
{
    double freq, amplitude, rate, phase = 0.0;
    float next()
    {
        const float v = static_cast<float>(amplitude * std::sin(phase));
        phase += 2.0 * kPi * freq / rate;
        if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
        return v;
    }
};

// Paul Kellet's refined pink-noise filter over a deterministic LCG.
struct PinkNoise
{
    explicit PinkNoise(uint32_t seed) : state(seed) {}
    uint32_t state;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float next()
    {
        state = state * 1664525u + 1013904223u;
        const double white = (static_cast<double>(state) / 4294967296.0) * 2.0 - 1.0;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        return static_cast<float>(pink * 0.11);
    }
};

inline double rms(const float* x, size_t n)
{
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += double(x[i]) * x[i];
    return n ? std::sqrt(s / n) : 0.0;
}

inline double peak(const float* x, size_t n)
{
    double p = 0;
    for (size_t i = 0; i < n; ++i) p = std::max(p, std::abs(double(x[i])));
    return p;
}

// Amplitude of the component at `freq` (Goertzel, Hann-windowed).
inline double toneAmplitude(const float* x, size_t n, double freq, double rate)
{
    double re = 0, im = 0, wsum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
        const double a = 2 * kPi * freq * i / rate;
        re += w * x[i] * std::cos(a);
        im -= w * x[i] * std::sin(a);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

// Least-squares fit of a*sin + b*cos + c at a known frequency; returns the residual RMS.
inline double sineFitResidual(const float* x, size_t n, double freq, double rate)
{
    double ss = 0, sc = 0, cc = 0, s1 = 0, c1 = 0, xs = 0, xc = 0, x1 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double a = 2 * kPi * freq * i / rate, s = std::sin(a), c = std::cos(a);
        ss += s * s; sc += s * c; cc += c * c; s1 += s; c1 += c;
        xs += x[i] * s; xc += x[i] * c; x1 += x[i];
    }
    // Solve the 3×3 normal equations (Cramer's rule).
    const double m[3][3] = {{ss, sc, s1}, {sc, cc, c1}, {s1, c1, double(n)}};
    const double v[3] = {xs, xc, x1};
    auto det3 = [](const double a[3][3]) {
        return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
               a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    };
    const double d = det3(m);
    double coef[3];
    for (int k = 0; k < 3; ++k)
    {
        double t[3][3];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) t[r][c] = (c == k) ? v[r] : m[r][c];
        coef[k] = det3(t) / d;
    }
    double err = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double a = 2 * kPi * freq * i / rate;
        const double e = x[i] - (coef[0] * std::sin(a) + coef[1] * std::cos(a) + coef[2]);
        err += e * e;
    }
    return std::sqrt(err / n);
}

// Largest |second difference| — a click detector for smooth signals.
inline double maxSecondDifference(const float* x, size_t n)
{
    double m = 0;
    for (size_t i = 2; i < n; ++i) m = std::max(m, std::abs(double(x[i]) - 2.0 * x[i - 1] + x[i - 2]));
    return m;
}

// Ideal max |second difference| of a sine of amplitude A at f.
inline double sineSecondDifference(double amplitude, double freq, double rate)
{
    const double w = 2 * kPi * freq / rate;
    return amplitude * 2.0 * (1.0 - std::cos(w));
}

} // namespace pf8test
