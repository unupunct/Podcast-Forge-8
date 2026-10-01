#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "Analysis.h"
#include "EngineHarness.h"
#include "routing/TalkbackKey.h"

using namespace pf8;
using namespace pf8test;

TEST_CASE("TalkbackKey: hold talks, a tap latches, the next press unlatches", "[talkback]")
{
    TalkbackKey k;
    k.press(0);
    CHECK(k.active());
    k.release(1000); // held 1 s → momentary
    CHECK_FALSE(k.active());

    k.press(2000);
    k.release(2100); // tap → latched
    CHECK(k.active());
    CHECK(k.latched());
    k.press(5000); // next press unlatches immediately
    CHECK_FALSE(k.active());
    k.release(5050); // and its release does not re-latch
    CHECK_FALSE(k.active());
    CHECK_FALSE(k.latched());

    k.press(6000);
    k.press(6100); // key repeat ignored
    k.release(7000);
    CHECK_FALSE(k.active());
}

TEST_CASE("TalkbackKey: momentary and latch modes", "[talkback]")
{
    TalkbackKey m;
    m.setMode(TalkbackMode::Momentary);
    m.press(0);
    m.release(50); // a tap never latches
    CHECK_FALSE(m.active());

    TalkbackKey l;
    l.setMode(TalkbackMode::Latch);
    l.press(0);
    l.release(5000);
    CHECK(l.active());
    l.press(6000);
    l.release(6010);
    CHECK_FALSE(l.active());
    l.press(7000);
    l.setMode(TalkbackMode::Momentary); // switching to momentary drops a latch
    CHECK_FALSE(l.active());
}

namespace {
FakeDeviceSpec device(const char* name, int channels, bool master, std::function<float(int, double)> sig = {})
{
    FakeDeviceSpec s;
    s.name = name;
    s.period = 480;
    s.channels = channels;
    s.master = master;
    s.signal = std::move(sig);
    return s;
}
} // namespace

TEST_CASE("Talkback mic reaches only the target headphones, never Main, and only while keyed", "[harness][talkback]")
{
    EngineHarness h(48000, 128);
    h.addInput(device("ch1-mic", 1, false, [](int, double) { return 0.0f; }));
    h.addInput(device("producer-mic", 1, false, [](int, double t) { return static_cast<float>(0.5 * std::sin(2 * kPi * 1000.0 * t)); }));
    h.addOutput(device("hp1", 2, true));
    h.addOutput(device("hp2", 2, false));
    h.route(0, 0, -1, 0);
    h.route(1, -1, -1, 1);
    h.routeTalkback(1, -1);
    auto& rp = h.engine().routing();
    rp.talkbackSource = -1;
    rp.talkbackTarget[0] = true;
    rp.talkbackActive = true;
    h.commitGraph();
    h.captureFrom(2.0);
    h.run(3.0);

    const auto& hp1 = h.outputCapture(0);
    const auto& hp2 = h.outputCapture(1);
    REQUIRE(hp1.size() > 24000);
    REQUIRE(hp2.size() > 24000);
    CHECK(toneAmplitude(hp1.data() + 4800, 16384, 1000.0, 48000) > 0.2);
    CHECK(peak(hp2.data() + 4800, hp2.size() - 4800) < 1e-4f);
    const auto m = h.engine().meters();
    CHECK(m.busPeak[idx(BusId::Main)][0] == 0.0f);
    CHECK(m.busPeak[idx(BusId::Clean)][0] == 0.0f);
    CHECK(m.talkbackPeak > 0.45f);

    // Key released: gone from HP1 after the 5 ms ramp.
    const size_t before = hp1.size();
    rp.talkbackActive = false;
    h.captureFrom(3.1);
    h.run(0.5);
    const auto& after = h.outputCapture(0);
    REQUIRE(after.size() > before + 19200);
    const size_t from = before + 9600; // skip ramp + bridge latency
    CHECK(peak(after.data() + from, after.size() - from) < 1e-4f);
    CHECK(h.realtimeAllocations() == 0);
}

TEST_CASE("Talkback can use a channel's processed mic as its source", "[harness][talkback]")
{
    EngineHarness h(48000, 128);
    h.addInput(device("ch3-mic", 1, false, [](int, double t) { return static_cast<float>(0.3 * std::sin(2 * kPi * 500.0 * t)); }));
    h.addOutput(device("hp1", 2, true));
    h.route(0, -1, -1, 0);
    h.route(2, 0, -1, -1);
    for (int c = 0; c < kNumChannels; ++c) h.engine().dsp(c).limiterOn = false;
    auto& rp = h.engine().routing();
    rp.talkbackSource = 2;
    h.commitGraph();
    h.run(2.0);
    CHECK(std::abs(h.engine().meters().talkbackPeak - h.engine().meters().peak[2]) < 1e-6f);
    CHECK(h.engine().meters().talkbackPeak > 0.25f);
}

TEST_CASE("Custom headphone mix: a centred channel arrives at -3 dB (pan law)", "[harness][headphones]")
{
    EngineHarness h(48000, 128);
    h.addOutput(device("hp8", 2, true));
    h.route(7, -1, -1, 0);
    auto& e = h.engine();
    e.setSimulatedSource(0, 410.0f, 0.1f);
    auto& rp = e.routing();
    rp.headphones[7].mode = HpMode::Custom;
    for (int s = 0; s < kSourceCount; ++s) rp.gain[static_cast<size_t>(s)][hpBus(7)].set(0.0f);
    rp.gain[0][hpBus(7)].set(1.0f);
    h.commitGraph();
    h.captureFrom(1.0);
    h.run(2.0);
    const auto& y = h.outputCapture(0);
    REQUIRE(y.size() > 24000);
    const double a = toneAmplitude(y.data() + 4800, 16384, 410.0, 48000);
    INFO("HP8 left amplitude " << a << " (" << 20 * std::log10(a) << " dB)");
    CHECK(std::abs(20 * std::log10(a) - (-23.01)) < 0.3);
}
