#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <vector>

#include "Analysis.h"
#include "TestDirs.h"
#include "core/RealtimeGuard.h"
#include "media/AudioFileLoader.h"
#include "media/Soundboard.h"

using namespace pf8;
using Catch::Approx;

namespace {
std::shared_ptr<CartBuffer> constantCart(float value, int64_t frames)
{
    auto b = std::make_shared<CartBuffer>();
    b->samples.assign(static_cast<size_t>(frames) * 2, value);
    b->frames = frames;
    return b;
}

// Renders `frames` frames in 256-blocks, returns the left channel.
std::vector<float> render(Soundboard& sb, int frames)
{
    std::vector<float> outL, l(256), r(256);
    for (int done = 0; done < frames; done += 256)
    {
        sb.render(l.data(), r.data(), 256);
        outL.insert(outL.end(), l.begin(), l.end());
    }
    outL.resize(static_cast<size_t>(frames));
    return outL;
}

void writeTestFile(const std::filesystem::path& p, juce::AudioFormat& fmt, double rate, int channels, double freq, double seconds)
{
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(juce::File(juce::String(p.wstring().c_str())));
    auto w = fmt.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(channels).withBitsPerSample(24));
    REQUIRE(w != nullptr);
    const int n = static_cast<int>(rate * seconds);
    juce::AudioBuffer<float> b(channels, n);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i) b.setSample(c, i, static_cast<float>(0.5 * std::sin(2 * pf8test::kPi * freq * i / rate)));
    REQUIRE(w->writeFromAudioSampleBuffer(b, 0, n));
}
} // namespace

TEST_CASE("Soundboard: play, fade-in, fade-out and stop envelopes", "[media]")
{
    Soundboard sb(48000);
    sb.setBuffer(0, constantCart(0.5f, 48000 * 5));
    sb.settings(0).fadeInMs.set(100.0f);
    sb.settings(0).fadeOutMs.set(200.0f);
    sb.play(0);
    auto a = render(sb, 9600);
    CHECK(a[0] < 0.01f);
    CHECK(a[2400] == Approx(0.25f).margin(0.01f)); // halfway through the 100 ms fade-in
    CHECK(a[9599] == Approx(0.5f).margin(1e-5f));
    CHECK(sb.state(0).playing.load());

    sb.fadeOut(0);
    auto b = render(sb, 4800 * 2 + 4800); // 300 ms
    CHECK(b[4800] == Approx(0.25f).margin(0.01f)); // halfway through 200 ms
    CHECK(b.back() == 0.0f);
    CHECK_FALSE(sb.state(0).playing.load());

    sb.play(0);
    render(sb, 9600);
    sb.stop(0);
    auto c = render(sb, 480);
    CHECK(c[100] > 0.0f);        // 5 ms ramp, not a click
    CHECK(c[300] == 0.0f);
    float maxStep = 0.0f;
    for (size_t i = 1; i < c.size(); ++i) maxStep = std::max(maxStep, std::abs(c[i] - c[i - 1]));
    CHECK(maxStep < 0.003f);
}

TEST_CASE("Soundboard: retrigger restarts, one-shot ends, loop repeats", "[media]")
{
    Soundboard sb(48000);
    auto buf = std::make_shared<CartBuffer>();
    buf->frames = 1000;
    for (int i = 0; i < 1000; ++i) buf->samples.insert(buf->samples.end(), {static_cast<float>(i) / 1000.0f, 0.0f});
    sb.setBuffer(3, buf);
    sb.settings(3).fadeInMs.set(0.0f);
    sb.play(3);
    auto a = render(sb, 512);
    CHECK(a[10] == Approx(0.010f));
    sb.play(3); // retrigger
    auto b = render(sb, 1024);
    CHECK(b[10] == Approx(0.010f));
    CHECK(b[1000] == 0.0f); // one-shot ended
    CHECK_FALSE(sb.state(3).playing.load());
    sb.settings(3).loop = true;
    sb.play(3);
    auto c = render(sb, 2048);
    CHECK(c[1010] == Approx(0.010f)); // looped
}

TEST_CASE("Soundboard: 24 simultaneous carts render without allocating", "[media][rt]")
{
    Soundboard sb(48000);
    for (int i = 0; i < kCartCount; ++i)
    {
        sb.setBuffer(i, constantCart(0.01f, 48000));
        sb.settings(i).fadeInMs.set(0.0f);
        sb.play(i);
    }
    std::vector<float> l(512), r(512);
    rt::resetCounters();
    {
        rt::ScopedRealtime scope;
        for (int k = 0; k < 50; ++k) sb.render(l.data(), r.data(), 512);
    }
    CHECK(rt::allocationsOnRealtimeThreads() == 0);
    CHECK(l[100] == Approx(0.24f).margin(1e-5f)); // 24 × 0.01
}

TEST_CASE("Soundboard: a replaced buffer is freed only after the audio thread lets go", "[media]")
{
    Soundboard sb(48000);
    auto first = constantCart(0.3f, 48000);
    std::weak_ptr<const CartBuffer> watch = first;
    sb.setBuffer(0, first);
    first.reset();
    sb.settings(0).fadeInMs.set(0.0f);
    sb.play(0);
    render(sb, 512);
    sb.setBuffer(0, constantCart(0.6f, 48000)); // replace while playing
    sb.collectGarbage();
    CHECK_FALSE(watch.expired()); // still being played
    auto a = render(sb, 512);
    CHECK(a[0] == Approx(0.3f)); // keeps playing the old one until restarted
    sb.stop(0);
    render(sb, 1024);
    sb.collectGarbage();
    CHECK(watch.expired());
}

TEST_CASE("Audio file loader decodes WAV/FLAC, duplicates mono and converts the rate", "[media]")
{
    pf8test::TempDir dir("cartload");
    juce::WavAudioFormat wav;
    juce::FlacAudioFormat flac;
    const auto wavPath = dir.path() / "jingle.wav";
    const auto flacPath = dir.path() / "sting.flac";
    writeTestFile(wavPath, wav, 44100.0, 1, 1000.0, 1.0);
    writeTestFile(flacPath, flac, 48000.0, 2, 500.0, 0.5);

    auto a = loadAudioFile(wavPath, {48000, 600.0});
    REQUIRE(a->error.empty());
    CHECK(a->frames == 48000);
    std::vector<float> left(static_cast<size_t>(a->frames));
    for (int64_t i = 0; i < a->frames; ++i) left[static_cast<size_t>(i)] = a->samples[static_cast<size_t>(2 * i)];
    CHECK(pf8test::toneAmplitude(left.data() + 4800, 32768, 1000.0, 48000) == Approx(0.5).margin(0.01));
    CHECK(a->samples[48000] == Approx(a->samples[48001])); // mono duplicated

    auto b = loadAudioFile(flacPath, {48000, 600.0});
    REQUIRE(b->error.empty());
    CHECK(b->frames == 24000);

    auto c = loadAudioFile(dir.path() / "missing.mp3", {48000, 600.0});
    CHECK_FALSE(c->error.empty());
    auto d = loadAudioFile(wavPath, {48000, 0.5});
    CHECK_FALSE(d->error.empty()); // too long for a cart
}
