#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include <vector>

#include "Analysis.h"
#include "core/RealtimeGuard.h"
#include "dsp/VarResampler.h"

using namespace pf8;
using namespace pf8test;
using Catch::Approx;

namespace {
// Streams `seconds` of a sine at inRate through the resampler in blocks of `block` output frames.
std::vector<float> run(double inRate, double outRate, double freq, double seconds, int block,
                       double ppmAtHalf = 0.0)
{
    VarResampler rs;
    rs.prepare(1, inRate / outRate, block);
    Sine sine{freq, 0.5, inRate};
    std::vector<float> out, in;
    std::vector<float> buf(static_cast<size_t>(block));
    const size_t total = static_cast<size_t>(seconds * outRate);
    while (out.size() < total)
    {
        if (ppmAtHalf != 0.0 && out.size() >= total / 2) rs.setRatioCorrectionPpm(ppmAtHalf);
        const int need = rs.inputFramesNeeded(block);
        in.resize(static_cast<size_t>(need));
        for (auto& v : in) v = sine.next();
        REQUIRE(rs.pushInput(in.data(), need) == need);
        REQUIRE(rs.process(buf.data(), block) == block);
        out.insert(out.end(), buf.begin(), buf.end());
    }
    return out;
}
} // namespace

TEST_CASE("VarResampler upsampling 22.05k to 48k in large pushed chunks stays phase-continuous", "[engine][resampler]")
{
    // The music decoder's pattern: push 4096 input frames, drain with as many process() calls as
    // needed. A dropped or repeated input frame shows up as a step in the phase of the sine.
    const double inRate = 22050, outRate = 48000, f = 500;
    const int maxOut = static_cast<int>(std::ceil(4096.0 * outRate / inRate)) + VarResampler::kTaps + 16;
    VarResampler rs;
    rs.prepare(2, inRate / outRate, maxOut);
    std::vector<float> chunk(4096 * 2), out(static_cast<size_t>(maxOut) * 2), y;
    int64_t n = 0;
    for (int c = 0; c < 16; ++c)
    {
        for (int i = 0; i < 4096; ++i, ++n) chunk[2 * i] = chunk[2 * i + 1] = static_cast<float>(0.4 * std::sin(2 * kPi * f * n / inRate));
        int offset = 0;
        while (offset < 4096)
        {
            const int acc = rs.pushInput(chunk.data() + 2 * offset, 4096 - offset);
            offset += acc;
            int produced;
            do
            {
                produced = rs.process(out.data(), maxOut);
                for (int i = 0; i < produced; ++i) y.push_back(out[2 * i]);
            } while (produced == maxOut);
            REQUIRE((acc > 0 || produced > 0));
        }
    }
    // Any phase step (dropped / repeated input) makes the windowed tone amplitude dip.
    CHECK(static_cast<double>(y.size()) == Approx(16 * 4096 * outRate / inRate).margin(64));
    for (size_t at = 2048; at + 4096 <= y.size(); at += 1024)
    {
        INFO("at " << at << " (" << at / outRate << " s)");
        REQUIRE(toneAmplitude(y.data() + at, 4096, f, outRate) == Approx(0.4).margin(0.005));
    }
}

TEST_CASE("VarResampler passthrough at ratio 1 is exact with zero latency", "[engine][resampler]")
{
    VarResampler rs;
    rs.prepare(2, 1.0, 256, true);
    REQUIRE(rs.passthrough());
    std::vector<float> in(512), out(512);
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(i);
    REQUIRE(rs.inputFramesNeeded(256) == 256);
    rs.pushInput(in.data(), 256);
    REQUIRE(rs.process(out.data(), 256) == 256);
    REQUIRE(out == in);
}

TEST_CASE("VarResampler 1 ppm correction keeps a 1 kHz tone clean", "[engine][resampler]")
{
    auto out = run(48000, 48000, 1000, 2.0, 128, 1.0);
    const size_t skip = 4800;
    const double resid = sineFitResidual(out.data() + skip, 24000, 1000, 48000);
    CHECK(db(resid / 0.5) < -80.0);
}

TEST_CASE("VarResampler converts 44.1k to 48k and back with the tone intact", "[engine][resampler]")
{
    for (auto [inRate, outRate] : {std::pair{44100.0, 48000.0}, std::pair{48000.0, 44100.0}})
    {
        auto out = run(inRate, outRate, 1000, 1.5, 128);
        const size_t n = 32768, skip = 8192;
        const double amp = toneAmplitude(out.data() + skip, n, 1000, outRate);
        CHECK(std::abs(db(amp / 0.5)) < 0.1);
        CHECK(db(sineFitResidual(out.data() + skip, n, 1000, outRate) / 0.5) < -70.0);
    }
}

TEST_CASE("VarResampler ratio step produces no discontinuity", "[engine][resampler]")
{
    auto out = run(48000, 48000, 1000, 2.0, 128, 500.0);
    const double ideal = sineSecondDifference(0.5, 1000, 48000);
    CHECK(maxSecondDifference(out.data() + 4800, out.size() - 4800) < 1.5 * ideal);
}

TEST_CASE("VarResampler inputFramesNeeded is exact and process does not allocate", "[engine][resampler][rt]")
{
    VarResampler rs;
    rs.prepare(2, 44100.0 / 48000.0, 512);
    rs.setRatioCorrectionPpm(-250.0);
    std::vector<float> in(4096 * 2, 0.25f), out(512 * 2);
    rt::resetCounters();
    {
        rt::ScopedRealtime scope;
        for (int i = 0; i < 2000; ++i)
        {
            const int block = 64 + (i * 37) % 449;
            const int need = rs.inputFramesNeeded(block);
            rs.pushInput(in.data(), need);
            REQUIRE(rs.process(out.data(), block) == block);
            if (need > 0) REQUIRE(rs.inputFramesNeeded(1) >= 0);
        }
    }
    CHECK(rt::allocationsOnRealtimeThreads() == 0);
}
