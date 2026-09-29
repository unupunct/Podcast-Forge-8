#include "engine/VarResampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pf8 {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBeta = 8.0;             // Kaiser window β (~80 dB stopband)
constexpr double kMaxSlewPpmPerFrame = 0.02; // ≈ 1000 ppm/s at 48 kHz — a smooth glide, never a step

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

} // namespace

void VarResampler::prepare(int channels, double nominalRatio, int maxOutFrames, bool allowPassthrough)
{
    channels_ = std::max(1, channels);
    nominal_ = nominalRatio;
    passthrough_ = allowPassthrough && std::abs(nominalRatio - 1.0) < 1e-12;
    targetPpm_ = appliedPpm_ = 0.0;

    // Cutoff relative to the input Nyquist frequency; lowered when decimating.
    const double cutoff = 0.93 * std::min(1.0, 1.0 / nominalRatio);
    const double half = kTaps / 2.0;
    const double i0beta = besselI0(kBeta);
    table_.assign(static_cast<size_t>(kPhases + 1) * kTaps, 0.0f);
    for (int p = 0; p <= kPhases; ++p)
    {
        double sum = 0.0;
        double row[kTaps];
        for (int k = 0; k < kTaps; ++k)
        {
            const double t = static_cast<double>(p) / kPhases + (half - 1.0) - k; // distance from output centre
            const double x = cutoff * t;
            const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double w = t / half;
            const double win = std::abs(w) >= 1.0 ? 0.0 : besselI0(kBeta * std::sqrt(1.0 - w * w)) / i0beta;
            row[k] = sinc * win;
            sum += row[k];
        }
        for (int k = 0; k < kTaps; ++k)
            table_[static_cast<size_t>(p) * kTaps + k] = static_cast<float>(row[k] / sum); // unity DC gain per phase
    }

    // Worst-case ratio after correction is well inside ±1 %.
    const int maxIn = static_cast<int>(std::ceil(maxOutFrames * nominalRatio * 1.01)) + kTaps + 8;
    capacityFrames_ = kTaps + 2 * maxIn;
    buffer_.assign(static_cast<size_t>(capacityFrames_) * channels_, 0.0f);
    reset();
}

void VarResampler::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    appliedPpm_ = targetPpm_;
    if (passthrough_)
    {
        buffered_ = 0;
        pos_ = 0.0;
    }
    else
    {
        // Zero history so the first output is centred on the first real input sample.
        buffered_ = kTaps / 2 - 1;
        pos_ = static_cast<double>(kTaps / 2 - 1);
    }
}

double VarResampler::nextRatio(int outFrames) const noexcept
{
    const double maxStep = kMaxSlewPpmPerFrame * outFrames;
    double ppm = appliedPpm_;
    const double d = targetPpm_ - ppm;
    ppm += d > maxStep ? maxStep : (d < -maxStep ? -maxStep : d);
    return nominal_ * (1.0 + ppm * 1e-6);
}

int VarResampler::inputFramesNeeded(int outFrames) const noexcept
{
    if (outFrames <= 0) return 0;
    if (passthrough()) return std::max(0, outFrames - buffered_);
    const double r = nextRatio(outFrames);
    const double last = pos_ + (outFrames - 1) * r;
    const int needBuffered = static_cast<int>(std::floor(last)) + kTaps / 2 + 1;
    return std::max(0, needBuffered - buffered_);
}

int VarResampler::pushInput(const float* interleaved, int frames) noexcept
{
    if (buffered_ + frames > capacityFrames_) compact();
    const int n = std::min(frames, capacityFrames_ - buffered_);
    if (n <= 0) return 0;
    std::memcpy(buffer_.data() + static_cast<size_t>(buffered_) * channels_, interleaved,
                sizeof(float) * static_cast<size_t>(n) * channels_);
    buffered_ += n;
    return n;
}

int VarResampler::process(float* out, int outFrames) noexcept
{
    if (outFrames <= 0) return 0;

    if (passthrough())
    {
        const int n = std::min(outFrames, buffered_);
        std::memcpy(out, buffer_.data(), sizeof(float) * static_cast<size_t>(n) * channels_);
        std::memmove(buffer_.data(), buffer_.data() + static_cast<size_t>(n) * channels_,
                     sizeof(float) * static_cast<size_t>(buffered_ - n) * channels_);
        buffered_ -= n;
        return n;
    }

    const double r = nextRatio(outFrames);
    appliedPpm_ = (r / nominal_ - 1.0) * 1e6;

    const int ch = channels_;
    const int half = kTaps / 2;
    int produced = 0;
    for (; produced < outFrames; ++produced)
    {
        const double fl = std::floor(pos_);
        const int i = static_cast<int>(fl);
        if (i + half >= buffered_) break; // not enough input
        const double frac = (pos_ - fl) * kPhases;
        const int p = static_cast<int>(frac);
        const float mix = static_cast<float>(frac - p);
        const float* c0 = table_.data() + static_cast<size_t>(p) * kTaps;
        const float* c1 = c0 + kTaps;
        const float* src = buffer_.data() + static_cast<size_t>(i - half + 1) * ch;
        float* dst = out + static_cast<size_t>(produced) * ch;
        for (int c = 0; c < ch; ++c)
        {
            float a0 = 0.0f, a1 = 0.0f;
            const float* s = src + c;
            for (int k = 0; k < kTaps; ++k)
            {
                const float v = s[static_cast<size_t>(k) * ch];
                a0 += c0[k] * v;
                a1 += c1[k] * v;
            }
            dst[c] = a0 + (a1 - a0) * mix;
        }
        pos_ += r;
    }
    compact();
    return produced;
}

void VarResampler::compact() noexcept
{
    // Keep kTaps/2 - 1 frames of history before the next output centre.
    const int keepFrom = static_cast<int>(std::floor(pos_)) - (kTaps / 2 - 1);
    if (keepFrom <= 0) return;
    const int shift = std::min(keepFrom, buffered_);
    std::memmove(buffer_.data(), buffer_.data() + static_cast<size_t>(shift) * channels_,
                 sizeof(float) * static_cast<size_t>(buffered_ - shift) * channels_);
    buffered_ -= shift;
    pos_ -= shift;
}

} // namespace pf8
