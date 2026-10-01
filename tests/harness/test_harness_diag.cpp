// Diagnostic sweeps (hidden: run explicitly with "[.diag]").
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "Analysis.h"
#include "EngineHarness.h"
#include "engine/DriftController.h"

using namespace pf8;
using namespace pf8test;

namespace {
float click(double t)
{
    const double c = std::floor(t) + 0.5, x = (t - c) / 0.0002;
    return static_cast<float>(0.9 * std::exp(-x * x));
}

struct Result { double ppmStd, lagRange; bool locked; uint64_t xr; };

Result runOnce(double jitterUs)
{
    EngineHarness h(48000, 128);
    const double ppms[2] = {180.0, -120.0};
    for (int i = 0; i < 2; ++i)
    {
        FakeDeviceSpec in;
        in.ppm = ppms[i];
        in.period = 480;
        in.jitterUs = jitterUs;
        in.signal = [](int, double t) { return click(t); };
        h.addInput(in);
    }
    FakeDeviceSpec out;
    out.master = true;
    out.channels = 2;
    out.jitterUs = jitterUs;
    h.addOutput(out);
    for (int ch = 0; ch < 2; ++ch)
    {
        h.route(ch, ch, -1, 0);
        h.engine().dsp(ch).hpfOn = false;
    }
    h.commitGraph();
    h.run(60.0);
    std::vector<double> ppm;
    for (int i = 0; i < 120; ++i)
    {
        h.run(0.5);
        ppm.push_back(h.inputStats(0).ppm);
    }
    double m = 0, v = 0;
    for (double p : ppm) m += p;
    m /= static_cast<double>(ppm.size());
    for (double p : ppm) v += (p - m) * (p - m);
    const auto& c0 = h.clickPositions(0);
    const auto& c1 = h.clickPositions(1);
    double lo = 1e9, hi = -1e9;
    for (size_t k = c0.size() - 60; k < c0.size() && k < c1.size(); ++k)
    {
        lo = std::min(lo, c1[k] - c0[k]);
        hi = std::max(hi, c1[k] - c0[k]);
    }
    return {std::sqrt(v / static_cast<double>(ppm.size())), hi - lo, h.inputStats(0).status == SyncStatus::Locked,
            h.inputStats(0).underruns + h.inputStats(1).underruns};
}
} // namespace

TEST_CASE("DIAG drift tuning sweep", "[.diag]")
{
    struct T { double wn, zeta, tau, bw; };
    const T sweep[] = {{0.2, 1.4, 0.1, 0.5}, {0.2, 1.0, 0.2, 0.5}, {0.2, 1.2, 0.2, 0.2}, {0.2, 1.2, 0.2, 0.1},
                       {0.15, 1.2, 0.2, 0.1}, {0.2, 1.2, 0.3, 0.1}, {0.2, 1.2, 0.2, 0.05}};
    for (const T& t : sweep)
        for (double jit : {500.0, 3000.0})
        {
            DriftController::tuning() = {t.wn, t.zeta, t.tau, t.bw};
            const auto r = runOnce(jit);
            std::printf("wn=%.2f zeta=%.1f tau=%.2f dllBw=%.2f jitter=%4.0fus : ppm sd %6.2f  lag range %5.2f smp  locked %d  xr %llu\n",
                        t.wn, t.zeta, t.tau, t.bw, jit, r.ppmStd, r.lagRange, r.locked ? 1 : 0, static_cast<unsigned long long>(r.xr));
        }
    DriftController::tuning() = {};
}
