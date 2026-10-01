#include "media/AudioFileLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "dsp/VarResampler.h"

namespace pf8 {

const char* supportedAudioWildcard() noexcept { return "*.wav;*.mp3;*.flac"; }

std::shared_ptr<CartBuffer> loadAudioFile(const std::filesystem::path& file, const LoadOptions& o)
{
    auto out = std::make_shared<CartBuffer>();
    out->sourcePath = file.string();
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats(); // WAV, AIFF, FLAC, MP3 (JUCE_USE_MP3AUDIOFORMAT)
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(juce::File(juce::String(file.wstring().c_str()))));
    if (!reader)
    {
        out->error = "unsupported or unreadable file";
        return out;
    }
    const double seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    if (seconds > o.maxSeconds)
    {
        out->error = "longer than " + std::to_string(static_cast<int>(o.maxSeconds / 60)) + " minutes - use the music player";
        return out;
    }
    const int n = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buf(2, std::max(1, n));
    buf.clear();
    reader->read(&buf, 0, n, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1) buf.copyFrom(1, 0, buf, 0, 0, n);

    std::vector<float> inter(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i)
    {
        inter[static_cast<size_t>(2 * i)] = buf.getSample(0, i);
        inter[static_cast<size_t>(2 * i + 1)] = buf.getSample(1, i);
    }
    if (std::abs(reader->sampleRate - o.engineRate) < 0.5)
    {
        out->samples = std::move(inter);
        out->frames = n;
        return out;
    }
    // Rate conversion (offline use of the engine's resampler).
    VarResampler rs;
    constexpr int kBlock = 4096;
    rs.prepare(2, reader->sampleRate / o.engineRate, kBlock);
    const int64_t expected = static_cast<int64_t>(static_cast<double>(n) * o.engineRate / reader->sampleRate);
    out->samples.reserve(static_cast<size_t>(expected + kBlock) * 2);
    std::vector<float> block(static_cast<size_t>(kBlock) * 2);
    int64_t consumed = 0;
    const std::vector<float> zeros(static_cast<size_t>(VarResampler::kTaps) * 4, 0.0f);
    bool flushed = false;
    while (static_cast<int64_t>(out->samples.size() / 2) < expected)
    {
        const int need = rs.inputFramesNeeded(kBlock);
        if (need > 0)
        {
            const int avail = static_cast<int>(std::min<int64_t>(need, n - consumed));
            if (avail > 0)
            {
                rs.pushInput(inter.data() + consumed * 2, avail);
                consumed += avail;
            }
            if (avail < need)
            {
                if (flushed) break;
                rs.pushInput(zeros.data(), VarResampler::kTaps * 2); // flush the filter tail
                flushed = true;
            }
        }
        const int produced = rs.process(block.data(), kBlock);
        if (produced <= 0) break;
        out->samples.insert(out->samples.end(), block.begin(), block.begin() + produced * 2);
    }
    // Compensate the resampler's group delay (kLatency input frames) and trim to the exact length.
    const int64_t skip = static_cast<int64_t>(VarResampler::kLatency * o.engineRate / reader->sampleRate);
    if (static_cast<int64_t>(out->samples.size() / 2) > skip)
        out->samples.erase(out->samples.begin(), out->samples.begin() + skip * 2);
    if (static_cast<int64_t>(out->samples.size() / 2) > expected) out->samples.resize(static_cast<size_t>(expected) * 2);
    out->frames = static_cast<int64_t>(out->samples.size() / 2);
    return out;
}

} // namespace pf8
