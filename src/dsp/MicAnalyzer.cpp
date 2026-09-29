#include "dsp/MicAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace pf8::dsp {
namespace {
float db(double rms) { return rms > 1e-10 ? static_cast<float>(20.0 * std::log10(rms)) : -200.0f; }
} // namespace

void MicAnalyzer::prepare(double fs)
{
    fs_ = fs;
    windowLen_ = static_cast<int>(fs * 0.1);
    aw_.prepare(fs);
    phase_ = Phase::Idle;
}

void MicAnalyzer::begin(Phase phase)
{
    phase_ = phase;
    inWindow_ = 0;
    sumSq_ = sumSqA_ = 0.0;
    windowPeak_ = 0.0f;
    aw_.reset();
    if (phase == Phase::Noise)
    {
        noiseA_.clear();
        noiseRaw_.clear();
        noiseSamples_ = 0;
    }
    else if (phase == Phase::Speech)
    {
        speechRms_.clear();
        speechPeak_ = 0.0f;
        clips_ = 0;
        speechSamples_ = 0;
    }
}

double MicAnalyzer::collectedSeconds() const noexcept
{
    return static_cast<double>(phase_ == Phase::Noise ? noiseSamples_ : speechSamples_) / fs_;
}

void MicAnalyzer::feed(const float* x, int n)
{
    if (phase_ == Phase::Idle) return;
    for (int i = 0; i < n; ++i)
    {
        const float s = x[i];
        const float a = aw_.process(s);
        sumSq_ += static_cast<double>(s) * s;
        sumSqA_ += static_cast<double>(a) * a;
        windowPeak_ = std::max(windowPeak_, std::abs(s));
        if (phase_ == Phase::Speech)
        {
            speechPeak_ = std::max(speechPeak_, std::abs(s));
            if (std::abs(s) >= 0.98855f) ++clips_; // −0.1 dBFS
            ++speechSamples_;
        }
        else
            ++noiseSamples_;
        if (++inWindow_ >= windowLen_) closeWindow();
    }
}

void MicAnalyzer::closeWindow()
{
    const double rms = std::sqrt(sumSq_ / inWindow_);
    const double rmsA = std::sqrt(sumSqA_ / inWindow_);
    lastWindowDb_ = db(rms);
    // The A-weighting filter needs ~50 ms to settle after begin(): skip the first window.
    const bool settled = (phase_ == Phase::Noise ? noiseSamples_ : speechSamples_) > static_cast<size_t>(windowLen_);
    if (settled)
    {
        if (phase_ == Phase::Noise)
        {
            noiseA_.push_back(db(rmsA));
            noiseRaw_.push_back(db(rms));
        }
        else if (phase_ == Phase::Speech)
            speechRms_.push_back(db(rms));
    }
    inWindow_ = 0;
    sumSq_ = sumSqA_ = 0.0;
    windowPeak_ = 0.0f;
}

MicAnalysis MicAnalyzer::result() const
{
    MicAnalysis r;
    auto percentile = [](std::vector<float> v, double p) {
        if (v.empty()) return -120.0f;
        std::sort(v.begin(), v.end());
        const size_t i = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
        return v[std::min(i, v.size() - 1)];
    };
    if (!noiseA_.empty())
    {
        r.haveNoise = true;
        r.noiseFloorDb = percentile(noiseA_, 0.10);
        r.noiseLowBiasDb = percentile(noiseRaw_, 0.10) - r.noiseFloorDb;
    }
    if (!speechRms_.empty())
    {
        r.haveSpeech = true;
        r.peakDb = db(speechPeak_);
        r.clipCount = clips_;
        // Speech-gated average: windows at least 10 dB above the noise floor (or the loudest half).
        const float gate = r.haveNoise ? r.noiseFloorDb + 10.0f : percentile(speechRms_, 0.5);
        double energy = 0.0;
        int count = 0;
        for (float w : speechRms_)
            if (w >= gate)
            {
                energy += std::pow(10.0, w / 10.0);
                ++count;
            }
        r.averageDb = count > 0 ? static_cast<float>(10.0 * std::log10(energy / count)) : percentile(speechRms_, 0.9);
    }
    r.signalDetected = (r.haveSpeech && r.peakDb > -70.0f) || (r.haveNoise && r.noiseFloorDb > -110.0f);

    // Recommendations.
    if (!r.signalDetected)
    {
        r.advice.push_back("No signal detected: check that the microphone is connected, unmuted and assigned to this channel.");
        return r;
    }
    if (r.haveSpeech)
    {
        float trim = -18.0f - r.averageDb;          // speech average at −18 dBFS
        trim = std::min(trim, -6.0f - r.peakDb);    // and peaks no higher than −6 dBFS
        r.recommendedTrimDb = std::clamp(trim, -24.0f, 24.0f);
        if (r.clipCount > 0)
            r.advice.push_back("The microphone clipped " + std::to_string(r.clipCount) +
                               " times: lower the gain ON THE MICROPHONE / interface. Software trim cannot repair clipping.");
        if (-18.0f - r.averageDb > 18.0f)
            r.advice.push_back("Speech is very quiet: raise the gain on the microphone, or move closer (10-15 cm).");
        r.compThresholdDb = r.averageDb + r.recommendedTrimDb + 2.0f;
    }
    if (r.haveNoise)
    {
        if (r.noiseFloorDb + r.recommendedTrimDb > -50.0f)
            r.advice.push_back("The noise floor is high: reduce room noise, move closer to the microphone, or use a dynamic mic.");
        r.hpfHz = r.noiseLowBiasDb > 6.0f ? 100.0f : 80.0f;
        if (r.noiseLowBiasDb > 6.0f) r.advice.push_back("Low-frequency rumble detected: high-pass set to 100 Hz.");
        r.gateThresholdDb = std::clamp(r.noiseFloorDb + r.recommendedTrimDb + 10.0f, -80.0f, -20.0f);
    }
    r.limiterCeilingDb = -1.0f;
    if (r.advice.empty()) r.advice.push_back("Levels look good.");
    return r;
}

} // namespace pf8::dsp
