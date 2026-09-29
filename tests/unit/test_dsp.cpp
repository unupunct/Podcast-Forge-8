#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "Analysis.h"
#include "core/RealtimeGuard.h"
#include "dsp/Biquad.h"
#include "dsp/ChannelStrip.h"
#include "dsp/Dynamics.h"
#include "dsp/MicAnalyzer.h"
#include "dsp/Presets.h"
#include "dsp/Reverb.h"

using namespace pf8;
using namespace pf8::dsp;
using Catch::Approx;

namespace {
constexpr double kFs = 48000.0;

std::vector<float> sine(double f, double dbfs, double seconds)
{
    std::vector<float> x(static_cast<size_t>(seconds * kFs));
    const double a = std::pow(10.0, dbfs / 20.0);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(a * std::sin(2 * pf8test::kPi * f * static_cast<double>(i) / kFs));
    return x;
}

double tailPeakDb(const std::vector<float>& y, double seconds)
{
    const size_t n = static_cast<size_t>(seconds * kFs);
    return pf8test::db(pf8test::peak(y.data() + (y.size() - n), n));
}

// Runs a chain in 256-sample blocks (like the engine).
template <typename F>
void blocks(std::vector<float>& x, F&& fn)
{
    for (size_t i = 0; i < x.size(); i += 256) fn(x.data() + i, static_cast<int>(std::min<size_t>(256, x.size() - i)));
}
} // namespace

TEST_CASE("DSP: high-pass corner and slopes", "[dsp]")
{
    for (double fc : {80.0, 150.0, 300.0})
    {
        const auto c = BiquadCoeffs::highPass(kFs, fc, 0.7071068);
        CHECK(20 * std::log10(c.magnitude(kFs, fc)) == Approx(-3.01).margin(0.05));
        // −3 dB point within ±2 %.
        CHECK(20 * std::log10(c.magnitude(kFs, fc * 1.02)) > -3.01);
        CHECK(20 * std::log10(c.magnitude(kFs, fc * 0.98)) < -3.01);
        CHECK(20 * std::log10(c.magnitude(kFs, fc / 2)) == Approx(-12.3).margin(1.0));
        // 24 dB/oct cascade: one octave below ≈ −24 dB.
        const auto c0 = BiquadCoeffs::highPass(kFs, fc, 0.5411961), c1 = BiquadCoeffs::highPass(kFs, fc, 1.3065630);
        const double m24 = 20 * std::log10(c0.magnitude(kFs, fc / 2) * c1.magnitude(kFs, fc / 2));
        CHECK(m24 == Approx(-24.1).margin(1.0));
        CHECK(20 * std::log10(c0.magnitude(kFs, fc) * c1.magnitude(kFs, fc)) == Approx(-3.01).margin(0.1));
    }
}

TEST_CASE("DSP: peak EQ gain at the centre frequency", "[dsp]")
{
    for (double g : {-12.0, -3.0, 4.0, 18.0})
        for (double f : {300.0, 3500.0, 10000.0})
            CHECK(20 * std::log10(BiquadCoeffs::peak(kFs, f, 1.2, g).magnitude(kFs, f)) == Approx(g).margin(0.1));
    CHECK(20 * std::log10(BiquadCoeffs::lowShelf(kFs, 120, 0.7, 3.0).magnitude(kFs, 20)) == Approx(3.0).margin(0.3));
    CHECK(20 * std::log10(BiquadCoeffs::highShelf(kFs, 10000, 0.7, -2.0).magnitude(kFs, 20000)) == Approx(-2.0).margin(0.3));
}

TEST_CASE("DSP: A-weighting follows the IEC curve", "[dsp]")
{
    AWeighting aw;
    aw.prepare(kFs);
    auto gainAt = [&](double f) {
        aw.reset();
        auto x = sine(f, 0.0, 2.0);
        for (auto& v : x) v = aw.process(v);
        return tailPeakDb(x, 0.5);
    };
    CHECK(gainAt(1000) == Approx(0.0).margin(0.2));
    CHECK(gainAt(100) == Approx(-19.1).margin(0.5));
    // Bilinear-transform compression near Nyquist: the digital curve is ~0.5 dB high at 10 kHz.
    CHECK(gainAt(10000) == Approx(-2.5).margin(0.7));
}

TEST_CASE("DSP: compressor static curve matches the formula", "[dsp]")
{
    Compressor comp;
    comp.prepare(kFs);
    CompressorSettings s;
    s.thresholdDb = -20;
    s.ratio = 4;
    s.kneeDb = 6;
    s.makeupDb = 0;
    s.attackMs = 5;
    s.releaseMs = 100;
    comp.set(s);
    for (double in : {-40.0, -30.0, -24.0, -22.0, -20.0, -18.0, -15.0, -10.0, -5.0, -1.0})
    {
        comp.reset();
        auto x = sine(1000, in, 1.5);
        blocks(x, [&](float* p, int n) { comp.process(p, n); });
        const double expected = in + Compressor::staticGainDb(static_cast<float>(in), -20, 4, 6);
        INFO("input " << in << " dBFS");
        CHECK(tailPeakDb(x, 0.2) == Approx(expected).margin(0.2));
    }
}

TEST_CASE("DSP: noise gate opens on signal and closes to its range", "[dsp]")
{
    NoiseGate gate;
    gate.prepare(kFs);
    gate.set({-40, -30, 2, 50, 100});
    auto x = sine(1000, -20, 0.5);
    blocks(x, [&](float* p, int n) { gate.process(p, n); });
    CHECK(gate.open());
    CHECK(gate.gain() == Approx(1.0f).margin(1e-3));
    // Silence: detector decay + hold 50 ms + 5 × release 100 ms ≈ 0.6 s → at the range.
    std::vector<float> silence(static_cast<size_t>(0.7 * kFs), 0.0f);
    blocks(silence, [&](float* p, int n) { gate.process(p, n); });
    CHECK_FALSE(gate.open());
    CHECK(linToDb(gate.gain()) == Approx(-30.0).margin(1.0));
    // Still closed after hold + one release constant would be too early to reach the range:
    gate.reset();
    auto y = sine(1000, -20, 0.2);
    blocks(y, [&](float* p, int n) { gate.process(p, n); });
    std::vector<float> shortGap(static_cast<size_t>(0.04 * kFs), 0.0f);
    blocks(shortGap, [&](float* p, int n) { gate.process(p, n); });
    CHECK(gate.open()); // within the hold time
}

TEST_CASE("DSP: limiter output never exceeds the ceiling", "[dsp]")
{
    for (bool tp : {false, true})
    {
        Limiter lim;
        lim.prepare(kFs, 2, 512);
        lim.setCeilingDb(-1.0f);
        lim.setTruePeak(tp);
        CHECK(lim.latency() == 72);
        const float ceiling = dbToLin(-1.0f);
        auto l = sine(997, 20.0, 1.0), r = sine(1511, 14.0, 1.0);
        // Impulse train on top.
        for (size_t i = 1000; i < l.size(); i += 4801) l[i] = r[i] = 30.0f;
        float maxOut = 0.0f;
        for (size_t i = 0; i < l.size(); i += 256)
        {
            const int n = static_cast<int>(std::min<size_t>(256, l.size() - i));
            float* ch[2] = {l.data() + i, r.data() + i};
            lim.process(ch, n, true);
            maxOut = std::max({maxOut, static_cast<float>(pf8test::peak(ch[0], static_cast<size_t>(n))),
                               static_cast<float>(pf8test::peak(ch[1], static_cast<size_t>(n)))});
        }
        INFO("true peak " << tp << " max " << maxOut);
        CHECK(maxOut <= ceiling + 1e-6f);
        CHECK(maxOut > ceiling * 0.95f); // it limits, it doesn't just silence
    }
}

TEST_CASE("DSP: limiter passes quiet audio unchanged apart from its latency", "[dsp]")
{
    Limiter lim;
    lim.prepare(kFs, 1, 512);
    lim.setCeilingDb(-1.0f);
    auto x = sine(440, -12.0, 0.5);
    const auto ref = x;
    for (size_t i = 0; i < x.size(); i += 256)
    {
        float* ch[1] = {x.data() + i};
        lim.process(ch, static_cast<int>(std::min<size_t>(256, x.size() - i)), true);
    }
    for (size_t i = 1000; i < 2000; ++i) CHECK(x[i] == Approx(ref[i - 72]).margin(1e-6));
}

TEST_CASE("DSP: de-esser reduces sibilance and leaves the voice band alone", "[dsp]")
{
    auto run = [](double f) {
        DeEsser d;
        d.prepare(kFs);
        d.set({6500, -30, 6});
        auto x = sine(f, -20, 1.0);
        blocks(x, [&](float* p, int n) { d.process(p, n); });
        return tailPeakDb(x, 0.2);
    };
    CHECK(run(6500) == Approx(-26.0).margin(1.0)); // 10 dB over threshold, limited to the 6 dB amount
    CHECK(run(1000) == Approx(-20.0).margin(0.1));
}

TEST_CASE("DSP: channel strip bypass switching is click-free and latency is constant", "[dsp][rt]")
{
    ChannelStrip strip;
    strip.prepare(kFs, 512);
    ChannelDspParams p;
    p.hpfOn = true;
    CHECK(strip.latency() == 72);
    auto x = sine(440, -18, 3.0);
    rt::resetCounters();
    {
        rt::ScopedRealtime scope;
        size_t blk = 0;
        blocks(x, [&](float* ptr, int n) {
            if (blk == 60) { p.compOn = true; apply(CompPreset::Podcast, p); }
            if (blk == 150) p.eqOn = true;
            if (blk == 250) { p.compOn = false; p.gateOn = true; }
            if (blk == 350) p.hpfHz.set(150.0f);
            strip.process(ptr, n, p);
            ++blk;
        });
    }
    CHECK(rt::allocationsOnRealtimeThreads() == 0);
    const double ideal = pf8test::sineSecondDifference(std::pow(10.0, -18.0 / 20.0) * 2.5, 440, kFs); // with makeup headroom
    double worst = 0;
    size_t where = 0;
    for (size_t i = 4802; i < x.size(); ++i)
    {
        const double d = std::abs(double(x[i]) - 2.0 * x[i - 1] + x[i - 2]);
        if (d > worst) { worst = d; where = i; }
    }
    INFO("worst second difference " << worst << " at block " << where / 256 << " sample " << where);
    CHECK(worst < ideal);
}

TEST_CASE("DSP: presets load the documented values", "[dsp]")
{
    ChannelDspParams p;
    apply(CompPreset::Radio, p);
    CHECK(p.compOn.load());
    CHECK(p.compThresholdDb.get() == -26.0f);
    CHECK(p.compRatio.get() == 6.0f);
    CHECK(p.compMakeupDb.get() == 10.0f);
    apply(EqPreset::FemaleVoice, p);
    CHECK(p.eqOn.load());
    CHECK(p.eq[2].freq.get() == 5000.0f);
    CHECK(p.eq[2].gainDb.get() == 2.0f);
    CHECK(p.eq[0].type.load() == EqBandType::LowShelf);
    CHECK(p.hpfHz.get() == 100.0f);
    CHECK(name(CompPreset::AggressiveVoice) == "Aggressive Voice");
}

TEST_CASE("DSP: reverb tail decays and stays finite", "[dsp]")
{
    Reverb rv;
    rv.prepare(kFs);
    std::vector<float> in(static_cast<size_t>(kFs * 4), 0.0f), l(in.size(), 0.0f), r(in.size(), 0.0f);
    in[0] = 1.0f;
    rv.processAdd(in.data(), l.data(), r.data(), static_cast<int>(in.size()), 1.0f);
    const double early = pf8test::rms(l.data() + 2400, 24000), late = pf8test::rms(l.data() + 144000, 24000);
    CHECK(early > 1e-4);
    CHECK(late < early * 0.1);
    for (float v : l) REQUIRE(std::isfinite(v));
}

TEST_CASE("MicAnalyzer measures noise, speech and recommends trim", "[dsp][wizard]")
{
    MicAnalyzer a;
    a.prepare(kFs);
    a.begin(MicAnalyzer::Phase::Noise);
    pf8test::PinkNoise noise(3);
    std::vector<float> n(static_cast<size_t>(kFs * 5));
    for (auto& v : n) v = noise.next() * 0.01f; // ≈ −60 dBFS pink
    a.feed(n.data(), static_cast<int>(n.size()));
    a.begin(MicAnalyzer::Phase::Speech);
    // "Speech": 1 kHz bursts at −21 dBFS peak (−24 dBFS RMS), 60 % duty.
    auto s = sine(1000, -21, 10.0);
    for (size_t i = 0; i < s.size(); ++i)
        if ((i / 24000) % 5 >= 3) s[i] = n[i % n.size()];
    a.feed(s.data(), static_cast<int>(s.size()));
    const auto r = a.result();
    CHECK(r.signalDetected);
    CHECK(r.noiseFloorDb == Approx(-63.0).margin(5.0));
    CHECK(r.peakDb == Approx(-21.0).margin(0.2));
    CHECK(r.averageDb == Approx(-24.0).margin(0.5));
    CHECK(r.clipCount == 0);
    CHECK(r.recommendedTrimDb == Approx(6.0).margin(0.5)); // −24 → −18
    CHECK(r.compThresholdDb == Approx(-16.0).margin(0.6));

    MicAnalyzer b;
    b.prepare(kFs);
    b.begin(MicAnalyzer::Phase::Speech);
    auto hot = sine(1000, 3.0, 2.0);
    for (auto& v : hot) v = std::clamp(v, -1.0f, 1.0f);
    b.feed(hot.data(), static_cast<int>(hot.size()));
    const auto rb = b.result();
    CHECK(rb.clipCount > 0);
    CHECK(rb.recommendedTrimDb < 0.0f);
    bool mentionsHardware = false;
    for (auto& adv : rb.advice) mentionsHardware |= adv.find("ON THE MICROPHONE") != std::string::npos;
    CHECK(mentionsHardware);

    MicAnalyzer c;
    c.prepare(kFs);
    c.begin(MicAnalyzer::Phase::Speech);
    std::vector<float> silence(static_cast<size_t>(kFs), 0.0f);
    c.feed(silence.data(), static_cast<int>(silence.size()));
    CHECK_FALSE(c.result().signalDetected);
}
