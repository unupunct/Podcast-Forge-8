#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <windows.h>

#include <cmath>
#include <vector>

#include "Analysis.h"
#include "TestDirs.h"
#include "core/RealtimeGuard.h"
#include "dsp/Ducker.h"
#include "media/MusicPlayer.h"

using namespace pf8;
using Catch::Approx;

TEST_CASE("Ducker: music goes down under speech within the attack time and returns after hold + release", "[dsp][ducking]")
{
    dsp::Ducker d;
    d.prepare(48000);
    dsp::DuckerParams p; // −35 dBFS, −15 dB, 50 ms attack, 500 ms hold, 1500 ms release
    std::vector<float> voice(48000, 0.0f), gain(48000);
    // 1 s silence → unity.
    d.process(voice.data(), 48000, p, gain.data());
    CHECK(gain.back() == 1.0f);
    // 1 s of "speech" at −20 dBFS.
    pf8test::Sine s{300.0, 0.1, 48000.0};
    for (auto& v : voice) v = s.next();
    d.process(voice.data(), 48000, p, gain.data());
    const float depth = std::pow(10.0f, -15.0f / 20.0f);
    // Fully ducked well within: RMS rise (~20 ms) + qualifying 50 ms + 50 ms ramp.
    CHECK(gain[48000 * 150 / 1000] == Approx(depth).margin(1e-4));
    CHECK(gain[48000 * 20 / 1000] > 0.95f); // not instantly: no pumping on clicks
    // Silence again: held for 500 ms, then a 1.5 s return.
    std::fill(voice.begin(), voice.end(), 0.0f);
    std::vector<float> g2(48000 * 3);
    std::vector<float> v2(48000 * 3, 0.0f);
    d.process(v2.data(), static_cast<int>(v2.size()), p, g2.data());
    CHECK(g2[48000 * 400 / 1000] == Approx(depth).margin(1e-4)); // still held
    CHECK(g2[48000 * 1300 / 1000] > depth);                       // returning
    CHECK(g2[48000 * 1300 / 1000] < 0.9f);                        // gently
    CHECK(g2.back() == 1.0f);                                     // fully back
    p.on = false;
    std::fill(v2.begin(), v2.end(), 0.5f);
    d.process(v2.data(), 4800, p, g2.data());
    CHECK(g2[4799] == 1.0f); // off: never ducks
}

TEST_CASE("Ducker allocates nothing", "[dsp][ducking][rt]")
{
    dsp::Ducker d;
    d.prepare(48000);
    dsp::DuckerParams p;
    std::vector<float> v(512, 0.2f), g(512);
    rt::resetCounters();
    {
        rt::ScopedRealtime scope;
        for (int i = 0; i < 100; ++i) d.process(v.data(), 512, p, g.data());
    }
    CHECK(rt::allocationsOnRealtimeThreads() == 0);
}

TEST_CASE("MusicPlayer plays low-rate files (22.05 / 16 kHz) without skipping", "[media][music]")
{
    // Regression: one resampler pass could not drain a 4096-frame chunk when the output expands by
    // more than 2x; the excess input was silently dropped (audible skips = phase steps).
    for (double fileRate : {22050.0, 16000.0})
    {
        INFO("file rate " << fileRate);
        pf8test::TempDir dir("music-lowrate");
        juce::WavAudioFormat wav;
        const auto p = dir.path() / "low.wav";
        {
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(juce::File(juce::String(p.wstring().c_str())));
            auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(fileRate).withNumChannels(1).withBitsPerSample(16));
            const int n = static_cast<int>(fileRate * 3);
            juce::AudioBuffer<float> b(1, n);
            for (int i = 0; i < n; ++i) b.setSample(0, i, static_cast<float>(0.4 * std::sin(2 * pf8test::kPi * 500.0 * i / fileRate)));
            w->writeFromAudioSampleBuffer(b, 0, n);
        }
        MusicPlayer mp(48000);
        mp.volume().set(1.0f);
        mp.autoAdvance() = false;
        mp.setPlaylist({p});
        mp.play(0);
        std::vector<float> l(480), r(480), all;
        for (int k = 0; k < 250; ++k) // 2.5 s
        {
            mp.render(l.data(), r.data(), 480, nullptr);
            all.insert(all.end(), l.begin(), l.end());
            Sleep(1);
        }
        CHECK(mp.status().underruns == 0);
        // Playback starts once the decoder has opened the file (start-up latency is not a skip).
        size_t start = 0;
        while (start < all.size() && std::abs(all[start]) < 0.01f) ++start;
        REQUIRE(start < 48000 * 3 / 10); // audible within 0.3 s
        // Everything rendered since then was played: nothing skipped, nothing repeated.
        CHECK(mp.status().position == Approx((all.size() - start) / 48000.0).margin(0.03));
        // The tone's phase is constant across 480-sample blocks (5 periods) once playing: a dropped
        // or repeated frame shows up as a step. Skip the start-up and the 50 ms fade-in.
        double prev = 0;
        int steps = 0;
        const size_t first = (start / 480 + 12) * 480;
        for (size_t b = first; b + 480 <= all.size(); b += 480)
        {
            double re = 0, im = 0;
            for (size_t i = 0; i < 480; ++i)
            {
                const double w = 2 * pf8test::kPi * 500.0 * static_cast<double>(b + i) / 48000.0;
                re += all[b + i] * std::cos(w);
                im += all[b + i] * std::sin(w);
            }
            const double ph = std::atan2(im, re);
            if (b > first && std::abs(std::remainder(ph - prev, 2 * pf8test::kPi)) > 0.02) ++steps;
            prev = ph;
        }
        CHECK(steps == 0);
    }
}
TEST_CASE("MusicPlayer streams a playlist continuously, pauses, fades and advances", "[media][music]")
{
    pf8test::TempDir dir("music");
    juce::WavAudioFormat wav;
    auto writeTone = [&](const char* name, double rate, double freq, double seconds) {
        const auto p = dir.path() / name;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(juce::File(juce::String(p.wstring().c_str())));
        auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
        const int n = static_cast<int>(rate * seconds);
        juce::AudioBuffer<float> b(2, n);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) b.setSample(c, i, static_cast<float>(0.4 * std::sin(2 * pf8test::kPi * freq * i / rate)));
        w->writeFromAudioSampleBuffer(b, 0, n);
        return p;
    };
    const auto a = writeTone("a.wav", 48000, 440, 1.0);
    const auto b = writeTone("b.wav", 44100, 880, 4.0);

    MusicPlayer mp(48000);
    mp.volume().set(1.0f);
    mp.setPlaylist({a, b});
    mp.play(0);
    std::vector<float> l(480), r(480), all;
    auto run = [&](double seconds) {
        for (int k = 0; k < static_cast<int>(seconds * 100); ++k)
        {
            mp.render(l.data(), r.data(), 480, nullptr);
            all.insert(all.end(), l.begin(), l.end());
            Sleep(1); // give the decoder thread time, like a real-time callback cadence
        }
    };
    run(0.5);
    CHECK(mp.status().index == 0);
    CHECK(mp.status().playing);
    CHECK(pf8test::toneAmplitude(all.data() + 9600, 8192, 440.0, 48000) == Approx(0.4).margin(0.02));

    mp.pause();
    all.clear();
    run(0.2);
    CHECK(pf8test::peak(all.data() + 2400, all.size() - 2400) == 0.0f); // silent after the 20 ms ramp
    const double pos = mp.status().position;
    mp.resume();
    all.clear();
    run(1.2); // finishes track A, auto-advances to B (44.1 kHz, resampled)
    CHECK(mp.status().position < pos + 1.2);
    CHECK(mp.status().index == 1);
    CHECK(mp.status().underruns == 0);

    all.clear();
    run(0.3);
    CHECK(pf8test::toneAmplitude(all.data(), 8192, 880.0, 48000) == Approx(0.4).margin(0.03)); // pitch kept across rates
    mp.fadeOut(100.0f);
    all.clear();
    run(0.3);
    CHECK(pf8test::peak(all.data() + 9600, all.size() - 9600) == 0.0f); // faded, then paused
    CHECK(mp.status().paused);
}
