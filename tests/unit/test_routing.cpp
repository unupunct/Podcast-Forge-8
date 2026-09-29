#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "Analysis.h"
#include "core/RealtimeGuard.h"
#include "routing/CoughMute.h"
#include "routing/RoutingEngine.h"

using namespace pf8;
using Catch::Approx;

namespace {

constexpr int kBlock = 256;
const float kCentre = std::cos(0.25f * 3.14159265f); // −3.01 dB

// Runs the routing engine block by block with per-channel constant (DC) inputs, returning the last
// block's buses — steady state after ramps.
struct Rig
{
    RoutingEngine eng;
    std::array<std::vector<float>, kRoutingChannels> ch;
    std::vector<float> music = std::vector<float>(kBlock, 0.0f), talk = std::vector<float>(kBlock, 0.0f);
    std::array<std::vector<float>, kBusCount> l, r;
    RoutingInputs in;
    RoutingOutputs out;

    Rig()
    {
        eng.prepare(48000, kBlock);
        for (auto& c : ch) c.assign(kBlock, 0.0f);
        for (int b = 0; b < kBusCount; ++b)
        {
            l[static_cast<size_t>(b)].assign(kBlock, 0.0f);
            r[static_cast<size_t>(b)].assign(kBlock, 0.0f);
            out.left[static_cast<size_t>(b)] = l[static_cast<size_t>(b)].data();
            out.right[static_cast<size_t>(b)] = r[static_cast<size_t>(b)].data();
        }
        for (int c = 0; c < kRoutingChannels; ++c) in.channel[static_cast<size_t>(c)] = ch[static_cast<size_t>(c)].data();
        in.musicL = in.musicR = music.data();
        in.talkback = talk.data();
    }
    void set(int channel, float v) { std::fill(ch[static_cast<size_t>(channel)].begin(), ch[static_cast<size_t>(channel)].end(), v); }
    void run(int blocks = 20)
    {
        for (int i = 0; i < blocks; ++i) eng.process(in, out, kBlock);
    }
    float L(BusId b) const { return l[static_cast<size_t>(idx(b))].back(); }
    float R(BusId b) const { return r[static_cast<size_t>(idx(b))].back(); }
    float hpL(int hp) const { return l[static_cast<size_t>(hpBus(hp))].back(); }
    RoutingParams& p() { return eng.params(); }
};

} // namespace

TEST_CASE("Routing invariant 1: default routing gains are exact", "[routing]")
{
    Rig rig;
    rig.set(2, 1.0f); // CH3
    rig.p().channel[2].fader.set(0.5f);
    rig.run();
    CHECK(rig.L(BusId::Main) == Approx(0.5f * kCentre).margin(1e-6));
    CHECK(rig.R(BusId::Main) == Approx(0.5f * kCentre).margin(1e-6));
    CHECK(rig.L(BusId::Clean) == Approx(0.5f * kCentre).margin(1e-6));
    // Headphone sends are pre-fader by default: self 1.0, others 0.7 (× the pan law).
    CHECK(rig.hpL(2) == Approx(1.0f * kCentre).margin(1e-6));
    for (int hp : {0, 1, 3, 7}) CHECK(rig.hpL(hp) == Approx(0.7f * kCentre).margin(1e-6));
    CHECK(rig.L(BusId::MusicOut) == 0.0f);
}

TEST_CASE("Routing: music reaches Main, MusicOut and headphones at 20 % but not Clean", "[routing]")
{
    Rig rig;
    std::fill(rig.music.begin(), rig.music.end(), 1.0f);
    rig.run();
    CHECK(rig.L(BusId::Main) == Approx(1.0f).margin(1e-6));
    CHECK(rig.L(BusId::MusicOut) == Approx(1.0f).margin(1e-6));
    CHECK(rig.L(BusId::Clean) == 0.0f);
    CHECK(rig.hpL(0) == Approx(0.2f).margin(1e-6));
}

TEST_CASE("Routing invariant 2: talkback never reaches the program while locked", "[routing]")
{
    Rig rig;
    std::fill(rig.talk.begin(), rig.talk.end(), 1.0f);
    rig.p().gain[idx(SourceId::Talkback)][idx(BusId::Main)].set(1.0f); // even if someone sets it
    rig.p().talkbackActive = true;
    rig.p().talkbackTarget[4] = true;
    rig.run();
    CHECK(rig.L(BusId::Main) == 0.0f);
    CHECK(rig.L(BusId::Clean) == 0.0f);
    CHECK(rig.hpL(4) == Approx(kCentre).margin(1e-6)); // target hears the producer
    CHECK(rig.hpL(3) == 0.0f);                         // others don't

    rig.p().talkbackActive = false;
    rig.run();
    CHECK(rig.hpL(4) == 0.0f);

    rig.p().talkbackToProgram = true;
    rig.p().talkbackActive = true;
    rig.run();
    CHECK(rig.L(BusId::Main) == Approx(kCentre).margin(1e-6)); // explicitly enabled
}

TEST_CASE("Routing: talkback dims the target headphones by 12 dB", "[routing]")
{
    Rig rig;
    rig.set(0, 1.0f);
    rig.p().talkbackActive = true;
    rig.p().talkbackTarget[0] = true; // talk to CH1's headphones; the talkback signal is silent here
    rig.run();
    CHECK(rig.hpL(0) == Approx(0.25f * kCentre).margin(1e-6));
    CHECK(rig.hpL(1) == Approx(0.7f * kCentre).margin(1e-6)); // not a target
}

TEST_CASE("Routing invariant 3: solo and PFL never change Main, Clean or headphones", "[routing]")
{
    auto run = [](bool soloPfl) {
        Rig rig;
        pf8test::PinkNoise noise(7);
        std::vector<std::vector<float>> captured(kBusCount);
        for (int blk = 0; blk < 40; ++blk)
        {
            for (int c = 0; c < kRoutingChannels; ++c)
                for (auto& v : rig.ch[static_cast<size_t>(c)]) v = noise.next();
            if (soloPfl && blk == 10)
            {
                rig.p().channel[1].solo = true;
                rig.p().channel[5].pfl = true;
            }
            rig.eng.process(rig.in, rig.out, kBlock);
            for (int b = 0; b < idx(BusId::Pfl); ++b)
                captured[static_cast<size_t>(b)].insert(captured[static_cast<size_t>(b)].end(), rig.l[static_cast<size_t>(b)].begin(),
                                                        rig.l[static_cast<size_t>(b)].end());
        }
        return captured;
    };
    auto a = run(false), b = run(true);
    for (int bus = 0; bus < idx(BusId::Pfl); ++bus) CHECK(a[static_cast<size_t>(bus)] == b[static_cast<size_t>(bus)]);
}

TEST_CASE("Routing: solo-in-place and PFL drive only the monitor", "[routing]")
{
    Rig rig;
    rig.set(0, 1.0f);
    rig.set(1, 0.5f);
    rig.p().channel[1].solo = true;
    rig.run();
    CHECK(rig.L(BusId::Monitor) == Approx(0.5f * kCentre).margin(1e-6)); // only CH2
    CHECK(rig.L(BusId::Main) == Approx(1.5f * kCentre).margin(1e-6));    // program unchanged

    rig.p().channel[0].pfl = true; // auto-PFL takes the monitor
    rig.run();
    CHECK(rig.eng.effectiveMonitorSource() == MonitorSource::Pfl);
    CHECK(rig.L(BusId::Monitor) == Approx(1.0f).margin(1e-6));
    CHECK(rig.L(BusId::Pfl) == Approx(1.0f).margin(1e-6));
    rig.p().channel[0].pfl = false;
    rig.p().channel[1].solo = false;
    rig.run();
    CHECK(rig.L(BusId::Monitor) == Approx(1.5f * kCentre).margin(1e-6));
}

TEST_CASE("Routing invariant 4: mute removes a channel from every bus but not its input", "[routing]")
{
    Rig rig;
    rig.set(3, 1.0f);
    rig.p().channel[3].mute = true;
    rig.run();
    CHECK(rig.L(BusId::Main) == 0.0f);
    CHECK(rig.L(BusId::Clean) == 0.0f);
    for (int hp = 0; hp < kRoutingChannels; ++hp) CHECK(rig.hpL(hp) == 0.0f);
    CHECK(rig.ch[3].back() == 1.0f); // the record tap (the channel input) is untouched by routing

    // Cough behaves the same through its own flag.
    rig.p().channel[3].mute = false;
    rig.p().channel[3].coughOpen = false;
    rig.run();
    CHECK(rig.L(BusId::Main) == 0.0f);
    rig.p().channel[3].coughOpen = true;
    rig.run();
    CHECK(rig.L(BusId::Main) == Approx(kCentre).margin(1e-6));
}

TEST_CASE("Routing invariant 5: gain changes ramp without a step", "[routing]")
{
    Rig rig;
    rig.set(0, 1.0f);
    rig.run();
    rig.p().channel[0].fader.set(0.0f);
    std::vector<float> y;
    for (int blk = 0; blk < 4; ++blk)
    {
        rig.eng.process(rig.in, rig.out, kBlock);
        y.insert(y.end(), rig.l[idx(BusId::Main)].begin(), rig.l[idx(BusId::Main)].end());
    }
    const float slope = kCentre / 480.0f; // full scale over 10 ms at 48 kHz
    float maxStep = 0.0f;
    for (size_t i = 1; i < y.size(); ++i) maxStep = std::max(maxStep, std::abs(y[i] - y[i - 1]));
    CHECK(maxStep <= slope * 1.01f);
    CHECK(y.back() == 0.0f);
}

TEST_CASE("Routing: constant-power pan law", "[routing]")
{
    Rig rig;
    rig.set(0, 1.0f);
    rig.p().channel[0].pan.set(-1.0f);
    rig.run();
    CHECK(rig.L(BusId::Main) == Approx(1.0f).margin(1e-6));
    CHECK(rig.R(BusId::Main) == Approx(0.0f).margin(1e-6));
    rig.p().channel[0].pan.set(0.0f);
    rig.run();
    CHECK(20.0 * std::log10(rig.L(BusId::Main)) == Approx(-3.0103).margin(1e-3));
}

TEST_CASE("Routing: headphone Main mode, volume and mute", "[routing]")
{
    Rig rig;
    rig.set(0, 1.0f);
    rig.set(1, 1.0f);
    rig.p().headphones[5].mode = HpMode::Main;
    rig.p().headphones[5].volume.set(0.5f);
    rig.run();
    CHECK(rig.hpL(5) == Approx(0.5f * rig.L(BusId::Main)).margin(1e-6));
    rig.p().headphones[5].mute = true;
    rig.run();
    CHECK(rig.hpL(5) == 0.0f);
    // Custom edits of one headphone mix don't touch another.
    rig.p().gain[0][hpBus(2)].set(0.0f);
    rig.run();
    CHECK(rig.hpL(2) == Approx(0.7f * kCentre).margin(1e-6)); // CH2 only
    CHECK(rig.hpL(3) == Approx(1.4f * kCentre).margin(1e-6));
}

TEST_CASE("Routing: monitor dim, mono, mute and master fader", "[routing]")
{
    Rig rig;
    rig.set(0, 1.0f);
    rig.p().channel[0].pan.set(-1.0f);
    rig.p().monitor.mono = true;
    rig.run();
    CHECK(rig.L(BusId::Monitor) == Approx(0.5f).margin(1e-6));
    CHECK(rig.R(BusId::Monitor) == Approx(0.5f).margin(1e-6));
    rig.p().monitor.dim = true;
    rig.run();
    CHECK(rig.L(BusId::Monitor) == Approx(0.05f).margin(1e-6));
    rig.p().monitor.mute = true;
    rig.run();
    CHECK(rig.L(BusId::Monitor) == 0.0f);
    rig.p().masterFader.set(0.25f);
    rig.run();
    CHECK(rig.L(BusId::Main) == Approx(0.25f).margin(1e-6));
}

TEST_CASE("Routing: process does not allocate", "[routing][rt]")
{
    Rig rig;
    rig.set(0, 0.3f);
    rig.p().channel[0].solo = true;
    rig.p().channel[1].pfl = true;
    rt::resetCounters();
    {
        rt::ScopedRealtime scope;
        rig.run(50);
    }
    CHECK(rt::allocationsOnRealtimeThreads() == 0);
}

TEST_CASE("CoughMute state machine", "[routing][cough]")
{
    CoughMute ptm(CoughMode::PushToMute);
    CHECK(ptm.open());
    ptm.keyDown();
    CHECK_FALSE(ptm.open());
    ptm.keyDown(); // auto-repeat
    ptm.keyUp();
    CHECK(ptm.open());

    CoughMute ptt(CoughMode::PushToTalk);
    CHECK_FALSE(ptt.open());
    ptt.keyDown();
    CHECK(ptt.open());
    ptt.releaseAll();
    CHECK_FALSE(ptt.open());

    CoughMute tog(CoughMode::Toggle);
    CHECK(tog.open());
    tog.keyDown();
    tog.keyUp();
    CHECK_FALSE(tog.open());
    tog.keyDown();
    tog.keyUp();
    CHECK(tog.open());
}
