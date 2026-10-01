#include "EngineHarness.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/RealtimeGuard.h"

namespace pf8test {

using namespace pf8;

namespace {
struct RtScope
{
    explicit RtScope(uint64_t& total) : total_(total), before_(rt::allocationsOnRealtimeThreads()) {}
    ~RtScope() { total_ += rt::allocationsOnRealtimeThreads() - before_; }
    uint64_t& total_;
    uint64_t before_;
    rt::ScopedRealtime scope_;
};
} // namespace

EngineHarness::EngineHarness(int engineRate, int engineBlock)
    : engineRate_(engineRate), engineBlock_(engineBlock), engine_(engineRate, engineBlock)
{
    engine_.setClock(&EngineHarness::clockFn, this);
    engine_.setTap(this);
    for (auto& r : routes_) r = ChannelRoute{};
}

EngineHarness::~EngineHarness() { engine_.setTap(nullptr); }

int64_t EngineHarness::clockFn(void* ctx) noexcept
{
    return static_cast<int64_t>(static_cast<EngineHarness*>(ctx)->nowSec_ * 1e9);
}

BridgeConfig EngineHarness::configFor(const FakeDeviceSpec& s) const
{
    BridgeConfig c;
    c.deviceChannels = s.channels;
    c.deviceRate = s.rate;
    c.devicePeriod = s.period;
    c.engineRate = engineRate_;
    c.engineBlock = engineBlock_;
    c.engineBurst = masterBurst_;
    c.master = s.master;
    return c;
}

int EngineHarness::addInput(const FakeDeviceSpec& spec)
{
    Device d;
    d.spec = spec;
    d.isInput = true;
    d.rng.seed(1000u + static_cast<unsigned>(inputs_.size()));
    d.buffer.assign(static_cast<size_t>(spec.period) * spec.channels, 0.0f);
    d.nextTime = static_cast<double>(spec.period) / (spec.rate * (1.0 + spec.ppm * 1e-6));
    if (spec.master) hasMaster_ = true;
    inputs_.push_back(std::move(d));
    return static_cast<int>(inputs_.size()) - 1;
}

int EngineHarness::addOutput(const FakeDeviceSpec& spec)
{
    Device d;
    d.spec = spec;
    d.isInput = false;
    d.rng.seed(2000u + static_cast<unsigned>(outputs_.size()));
    d.buffer.assign(static_cast<size_t>(spec.period) * spec.channels, 0.0f);
    d.nextTime = static_cast<double>(spec.period) / (spec.rate * (1.0 + spec.ppm * 1e-6));
    if (spec.master) hasMaster_ = true;
    outputs_.push_back(std::move(d));
    return static_cast<int>(outputs_.size()) - 1;
}

void EngineHarness::route(int channel, int input, int inputChannel, int output, int pair)
{
    auto& r = routes_[static_cast<size_t>(channel)];
    r.inputBridge = input; // device index; mapped to graph index in rebuildGraph
    r.inputChannel = inputChannel;
    r.outputBridge = output;
    r.outputPair = pair;
}

void EngineHarness::commitGraph()
{
    // Bridges are created once the master is known: non-master bridges buffer a master burst.
    masterBurst_ = engineBlock_;
    for (auto* list : {&inputs_, &outputs_})
        for (auto& d : *list)
            if (d.spec.master)
            {
                const double engineFrames = static_cast<double>(d.spec.period) * engineRate_ / d.spec.rate;
                masterBurst_ = static_cast<int>(std::ceil(engineFrames / engineBlock_)) * engineBlock_;
            }
    for (auto& d : inputs_)
        if (!d.in) d.in = std::make_shared<InputBridge>(configFor(d.spec));
    for (auto& d : outputs_)
        if (!d.out)
        {
            BridgeConfig c = configFor(d.spec);
            c.deviceBufferFrames = d.spec.master ? 0 : d.spec.period / 2; // packet in flight
            d.out = std::make_shared<OutputBridge>(c, std::max(1, d.spec.channels / 2));
        }
    rebuildGraph();
}

void EngineHarness::rebuildGraph()
{
    auto g = std::make_unique<EngineGraph>();
    g->generation = ++generation_;
    std::vector<int> inMap(inputs_.size(), -1), outMap(outputs_.size(), -1);
    for (size_t i = 0; i < inputs_.size(); ++i)
        if (inputs_[i].connected)
        {
            inMap[i] = static_cast<int>(g->inputs.size());
            g->inputs.push_back(inputs_[i].in);
        }
    for (size_t i = 0; i < outputs_.size(); ++i)
        if (outputs_[i].connected)
        {
            outMap[i] = static_cast<int>(g->outputs.size());
            g->outputs.push_back(outputs_[i].out);
        }
    for (size_t ch = 0; ch < routes_.size(); ++ch)
    {
        ChannelRoute r = routes_[ch];
        r.inputBridge = r.inputBridge >= 0 ? inMap[static_cast<size_t>(r.inputBridge)] : -1;
        r.outputBridge = r.outputBridge >= 0 ? outMap[static_cast<size_t>(r.outputBridge)] : -1;
        g->channels[ch] = r;
    }
    engine_.setGraph(std::move(g));
}

void EngineHarness::scheduleDisconnect(int input, double atSeconds) { events_.push_back({atSeconds, input, false}); }
void EngineHarness::scheduleReconnect(int input, double atSeconds) { events_.push_back({atSeconds, input, true}); }

double EngineHarness::callbackTime(Device& d)
{
    const double ideal = static_cast<double>(d.frames + static_cast<uint64_t>(d.spec.period)) /
                         (d.spec.rate * (1.0 + d.spec.ppm * 1e-6));
    if (d.spec.jitterUs <= 0) return ideal;
    std::uniform_real_distribution<double> u(0.0, d.spec.jitterUs * 1e-6);
    return ideal + u(d.rng);
}

void EngineHarness::fire(Device& d)
{
    const int64_t nowNs = static_cast<int64_t>(nowSec_ * 1e9);
    if (d.spec.timestampNoiseUs > 0)
    {
        std::uniform_real_distribution<double> step(-0.3, 0.3);
        const double lim = d.spec.timestampNoiseUs * 1e3;
        d.tsError = std::clamp(d.tsError + step(d.rng) * lim, -lim, lim);
    }
    const int n = d.spec.period;
    const int ch = d.spec.channels;
    if (d.isInput)
    {
        const double rate = d.spec.rate * (1.0 + d.spec.ppm * 1e-6);
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(d.frames + static_cast<uint64_t>(i)) / rate;
            for (int c = 0; c < ch; ++c)
                d.buffer[static_cast<size_t>(i) * ch + c] = d.spec.signal ? d.spec.signal(c, t) : 0.0f;
        }
        (void)rate;
        RtScope rt(rtAllocations_);
        d.in->deviceWrite(d.buffer.data(), n, nowNs + static_cast<int64_t>(d.tsError));
        if (d.spec.master) d.in->driveEngine(engine_, nowNs);
    }
    else
    {
        {
            RtScope rt(rtAllocations_);
            d.out->deviceRead(d.buffer.data(), n, nowNs + static_cast<int64_t>(d.tsError), d.spec.master ? &engine_ : nullptr);
        }
        if (nowSec_ >= captureFrom_)
            for (int i = 0; i < n; ++i) d.captured.push_back(d.buffer[static_cast<size_t>(i) * ch]);
    }
    d.frames += static_cast<uint64_t>(n);
    ++d.callbacks;
    d.nextTime = callbackTime(d);
}

void EngineHarness::run(double seconds)
{
    const double end = nowSec_ + seconds;
    // Pre-reserve capture storage so the tap never allocates on the tick.
    const double capSeconds = std::max(0.0, end - std::max(nowSec_, captureFrom_));
    for (size_t ch = 0; ch < channelCapture_.size(); ++ch)
        if (routes_[ch].inputBridge >= 0)
            channelCapture_[ch].reserve(channelCapture_[ch].size() + static_cast<size_t>(capSeconds * engineRate_) + 4 * kMaxBlock);
    for (auto& c : clicks_) c.reserve(c.size() + static_cast<size_t>(seconds) + 4);
    for (auto& o : outputs_) o.captured.reserve(o.captured.size() + static_cast<size_t>(capSeconds * o.spec.rate * 1.01) + 8192);
    if (!hasMaster_ && nextInternalTick_ < nowSec_) nextInternalTick_ = nowSec_;
    std::sort(events_.begin(), events_.end(), [](const Event& a, const Event& b) { return a.at < b.at; });

    uint64_t iterations = 0;
    for (;;)
    {
        double best = std::numeric_limits<double>::max();
        Device* next = nullptr;
        for (auto* list : {&inputs_, &outputs_})
            for (auto& d : *list)
                if (d.connected && d.nextTime < best)
                {
                    best = d.nextTime;
                    next = &d;
                }
        const double internalAt = hasMaster_ ? std::numeric_limits<double>::max() : nextInternalTick_;
        const double eventAt = events_.empty() ? std::numeric_limits<double>::max() : events_.front().at;
        const double t = std::min({best, internalAt, eventAt});
        if (t > end) break;
        nowSec_ = t;

        if (eventAt <= best && eventAt <= internalAt)
        {
            const Event e = events_.front();
            events_.erase(events_.begin());
            Device& d = inputs_[static_cast<size_t>(e.input)];
            if (e.connect)
            {
                d.in = std::make_shared<InputBridge>(configFor(d.spec));
                d.connected = true;
                d.frames = static_cast<uint64_t>(nowSec_ * d.spec.rate * (1.0 + d.spec.ppm * 1e-6));
                d.nextTime = callbackTime(d);
            }
            else
            {
                d.connected = false;
            }
            rebuildGraph();
        }
        else if (internalAt <= best)
        {
            {
                RtScope rt(rtAllocations_);
                engine_.tick(engineBlock_);
            }
            nextInternalTick_ += static_cast<double>(engineBlock_) / engineRate_;
        }
        else
        {
            fire(*next);
        }

        if (++iterations % 4096 == 0) engine_.collectGarbage();
    }
    nowSec_ = end;
    engine_.collectGarbage();
}

void EngineHarness::onChannelBlock(const float* const* channels, int numChannels, int frames) noexcept
{
    const bool capture = nowSec_ >= captureFrom_;
    const uint64_t window = static_cast<uint64_t>(engineRate_);
    for (int ch = 0; ch < numChannels && ch < kNumChannels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        const float* x = channels[ch];
        if (capture && routes_[c].inputBridge >= 0 &&
            channelCapture_[c].size() + static_cast<size_t>(frames) <= channelCapture_[c].capacity())
            channelCapture_[c].insert(channelCapture_[c].end(), x, x + frames);

        for (int i = 0; i < frames; ++i)
        {
            const uint64_t idx = engineFrames_ + static_cast<uint64_t>(i);
            // prev1_ is x[idx-1], prev2_ is x[idx-2]: a local maximum at idx-1 → parabolic fit.
            const float y0 = prev1_[c], ym1 = prev2_[c], yp1 = x[i];
            if (y0 > windowMax_[c] && y0 >= ym1 && y0 >= yp1 && y0 > 0.05f)
            {
                const double den = ym1 - 2.0 * y0 + yp1;
                const double off = den != 0.0 ? 0.5 * (ym1 - yp1) / den : 0.0;
                windowMax_[c] = y0;
                windowPos_[c] = static_cast<double>(idx - 1) + off;
            }
            prev2_[c] = prev1_[c];
            prev1_[c] = x[i];
            if ((idx + 1) % window == 0)
            {
                if (windowMax_[c] > 0.0 && clicks_[c].size() < clicks_[c].capacity()) clicks_[c].push_back(windowPos_[c]);
                windowMax_[c] = 0.0;
            }
        }
    }
    engineFrames_ += static_cast<uint64_t>(frames);
}

} // namespace pf8test
