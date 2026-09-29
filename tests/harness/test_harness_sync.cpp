#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "Analysis.h"
#include "EngineHarness.h"

using namespace pf8;
using namespace pf8test;

namespace {
// A smooth Gaussian click every second (at t = k + 0.5), sampled at the device's own clock.
float click(double t)
{
    const double c = std::floor(t) + 0.5;
    const double x = (t - c) / 0.0002;
    return static_cast<float>(0.9 * std::exp(-x * x));
}

FakeDeviceSpec input(const char* name, double ppm, int period, std::function<float(int, double)> sig)
{
    FakeDeviceSpec s;
    s.name = name;
    s.ppm = ppm;
    s.period = period;
    s.jitterUs = 500;
    s.signal = std::move(sig);
    return s;
}

FakeDeviceSpec output(const char* name, double ppm, int period, bool master = false, int channels = 2)
{
    FakeDeviceSpec s;
    s.name = name;
    s.ppm = ppm;
    s.period = period;
    s.channels = channels;
    s.master = master;
    s.jitterUs = master ? 0 : 500;
    return s;
}
} // namespace

TEST_CASE("Harness: six independent clocks stay locked for 10 minutes without glitches", "[harness][sync]")
{
    EngineHarness h(48000, 128);
    const double freqs[3] = {440.0, 1000.0, 1500.0};
    const double inPpm[3] = {200.0, -150.0, 80.0};
    const int inPeriod[3] = {480, 441, 128};
    for (int i = 0; i < 3; ++i)
    {
        const double f = freqs[i];
        h.addInput(input("mic", inPpm[i], inPeriod[i], [f](int, double t) { return static_cast<float>(0.25 * std::sin(2 * kPi * f * t)); }));
    }
    h.addOutput(output("hp-master", 0.0, 480, true));
    h.addOutput(output("hp2", 120.0, 480));
    h.addOutput(output("hp3", -90.0, 256, false, 1));
    for (int ch = 0; ch < 3; ++ch) h.route(ch, ch, -1, ch);
    // Headphones are routed mixes now; make each one self-only so its output is a single tone.
    for (int hp = 0; hp < 3; ++hp)
        for (int ch = 0; ch < kRoutingChannels; ++ch) h.engine().routing().gain[static_cast<size_t>(ch)][hpBus(hp)].set(ch == hp ? 1.0f : 0.0f);
    h.commitGraph();
    h.captureFrom(540.0); // keep the last minute
    h.run(600.0);

    for (int i = 0; i < 3; ++i)
    {
        const auto s = h.inputStats(i);
        INFO("input " << i << " ppm " << s.ppm << " fill " << s.fill << "/" << s.target);
        CHECK(s.status == SyncStatus::Locked);
        CHECK(std::abs(s.ppm - inPpm[i]) < 5.0);
        CHECK(s.underruns == 0);
        CHECK(s.overruns == 0);
    }
    for (int o = 1; o < 3; ++o)
    {
        const auto s = h.outputStats(o);
        INFO("output " << o << " ppm " << s.ppm << " fill " << s.fill << "/" << s.target);
        CHECK(s.status == SyncStatus::Locked);
        CHECK(s.underruns == 0);
    }
    CHECK(h.outputStats(0).underruns == 0);

    // Engine-side channels: pure tones, no clicks, at the device-clock-shifted frequency.
    for (int ch = 0; ch < 3; ++ch)
    {
        const auto& x = h.channelCapture(ch);
        REQUIRE(x.size() > 48000 * 50);
        const double ideal = sineSecondDifference(0.25, freqs[ch], 48000);
        INFO("channel " << ch);
        CHECK(maxSecondDifference(x.data(), x.size()) < 1.2 * ideal);
        // Device clock errors are corrected, so the engine sees the true pitch. Short windows,
        // since sub-ppm ratio wander (inaudible) moves the phase over long fits.
        for (size_t w = 0; w < 5; ++w)
            CHECK(db(sineFitResidual(x.data() + w * 480000, 9600, freqs[ch], 48000) / 0.25) < -60.0);
    }
    // Headphone outputs: continuous, no dropouts.
    for (int o = 0; o < 3; ++o)
    {
        const auto& y = h.outputCapture(o);
        REQUIRE(y.size() > 48000 * 50);
        INFO("output " << o);
        const double amplitude = 0.25 * 0.70710678; // centre pan law
        CHECK(maxSecondDifference(y.data(), y.size()) < 1.2 * sineSecondDifference(amplitude, freqs[o], 48000));
        CHECK(rms(y.data(), y.size()) > 0.9 * amplitude / std::sqrt(2.0));
    }
    CHECK(h.realtimeAllocations() == 0);
}

TEST_CASE("Harness: channels on different devices stay sample-aligned over time", "[harness][sync]")
{
    EngineHarness h(48000, 128);
    const double ppms[3] = {180.0, -200.0, 60.0};
    for (int i = 0; i < 3; ++i) h.addInput(input("mic", ppms[i], 480, [](int, double t) { return click(t); }));
    h.addOutput(output("hp-master", 0.0, 480, true));
    for (int ch = 0; ch < 3; ++ch) h.route(ch, ch, -1, 0);
    h.commitGraph();
    h.run(300.0);

    // After lock (last 150 clicks) the arrival-time difference between channels must be constant.
    const auto& c0 = h.clickPositions(0);
    REQUIRE(c0.size() > 250);
    for (int ch = 1; ch < 3; ++ch)
    {
        const auto& c = h.clickPositions(ch);
        REQUIRE(c.size() == c0.size());
        double lo = 1e9, hi = -1e9;
        for (size_t k = c.size() - 150; k < c.size(); ++k)
        {
            const double d = c[k] - c0[k];
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
        INFO("channel " << ch << " lag range " << lo << " .. " << hi);
        CHECK(hi - lo <= 1.0);
    }
    // And each channel's clicks arrive exactly one engine second apart (the device clocks are corrected).
    for (int ch = 0; ch < 3; ++ch)
    {
        const auto& c = h.clickPositions(ch);
        const double spacing = (c.back() - c[c.size() - 101]) / 100.0;
        INFO("channel " << ch << " spacing " << spacing);
        CHECK(std::abs(spacing - 48000.0) < 0.05);
    }
}

TEST_CASE("Harness: an unplugged mic goes silent, others are untouched, it returns to its channel", "[harness][hotplug]")
{
    auto build = [](EngineHarness& h) {
        const double f[3] = {300.0, 700.0, 1100.0};
        const double ppm[3] = {100.0, -100.0, 50.0};
        for (int i = 0; i < 3; ++i)
        {
            const double fi = f[i];
            h.addInput(input("mic", ppm[i], 480, [fi](int, double t) { return static_cast<float>(0.2 * std::sin(2 * kPi * fi * t)); }));
        }
        h.addOutput(output("hp-master", 0.0, 480, true));
        for (int ch = 0; ch < 3; ++ch) h.route(ch, ch, -1, 0);
        h.commitGraph();
    };

    EngineHarness ref(48000, 128), hp(48000, 128);
    build(ref);
    build(hp);
    hp.scheduleDisconnect(2, 5.0);   // channel 3's mic
    hp.scheduleReconnect(2, 8.0);
    ref.run(20.0);
    hp.run(20.0);

    // Channels 1 and 2 are bit-identical with and without the hot-plug event.
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& a = ref.channelCapture(ch);
        const auto& b = hp.channelCapture(ch);
        REQUIRE(a.size() == b.size());
        CHECK(std::equal(a.begin(), a.end(), b.begin()));
    }
    // Channel 3: silent while unplugged (after the graph swap), audio again after reconnect + priming.
    const auto& x = hp.channelCapture(2);
    CHECK(rms(x.data() + 48000 * 6, 48000) == 0.0);
    CHECK(rms(x.data() + 48000 * 12, 48000) > 0.1);
    CHECK(hp.realtimeAllocations() == 0);
}
