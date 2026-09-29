#include "engine/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Clock.h"
#include "core/Log.h"

namespace pf8 {
namespace {
int64_t defaultClock(void*) noexcept { return monotonicNs(); }
constexpr size_t kBridgeStride = static_cast<size_t>(EngineGraph::kMaxDeviceChannels) * kMaxBlock;
} // namespace

AudioEngine::AudioEngine(int sampleRate, int blockFrames)
    : sampleRate_(sampleRate), blockFrames_(std::clamp(blockFrames, 16, kMaxBlock)), clockFn_(&defaultClock)
{
    bridgeBuffers_.assign(EngineGraph::kMaxInputBridges * kBridgeStride, 0.0f);
    bridgePtrs_.resize(static_cast<size_t>(EngineGraph::kMaxInputBridges) * EngineGraph::kMaxDeviceChannels);
    for (size_t b = 0; b < EngineGraph::kMaxInputBridges; ++b)
        for (size_t c = 0; c < EngineGraph::kMaxDeviceChannels; ++c)
            bridgePtrs_[b * EngineGraph::kMaxDeviceChannels + c] = bridgeBuffers_.data() + b * kBridgeStride + c * kMaxBlock;
    channelBuffers_.assign(static_cast<size_t>(kNumChannels) * kMaxBlock, 0.0f);
    for (size_t c = 0; c < kNumChannels; ++c) channelPtrs_[c] = channelBuffers_.data() + c * kMaxBlock;

    routing_.prepare(sampleRate_, kMaxBlock);
    busBuffers_.assign(static_cast<size_t>(kBusCount) * 2 * kMaxBlock, 0.0f);
    for (size_t b = 0; b < kBusCount; ++b)
    {
        routingOut_.left[b] = busBuffers_.data() + (2 * b) * kMaxBlock;
        routingOut_.right[b] = busBuffers_.data() + (2 * b + 1) * kMaxBlock;
    }
    for (size_t c = 0; c < kNumChannels; ++c) routingIn_.channel[c] = channelPtrs_[c];
}

AudioEngine::~AudioEngine() { stopInternalClock(); }

void AudioEngine::setGraph(std::unique_ptr<EngineGraph> graph)
{
    EngineGraph* raw = graph.get();
    owned_.push_back(std::move(graph));
    while (!pending_.tryPush(raw)) collectGarbage(); // queue is large; only full if the tick is stalled
}

int AudioEngine::collectGarbage()
{
    int freed = 0;
    EngineGraph* g = nullptr;
    while (retired_.pop(&g, 1) == 1)
    {
        auto it = std::find_if(owned_.begin(), owned_.end(), [g](const auto& p) { return p.get() == g; });
        if (it != owned_.end())
        {
            owned_.erase(it);
            ++freed;
        }
    }
    return freed;
}

void AudioEngine::startInternalClock()
{
    if (internal_ && internal_->running()) return;
    internal_ = std::make_unique<InternalClock>(this, sampleRate_, blockFrames_);
    internal_->start();
    PF8_LOG_INFO("engine", "internal clock started rate=%d block=%d", sampleRate_, blockFrames_);
}

void AudioEngine::stopInternalClock()
{
    if (!internal_) return;
    internal_->stop();
    internal_.reset();
    PF8_LOG_INFO("engine", "internal clock stopped");
}

void AudioEngine::applyPendingGraph() noexcept
{
    EngineGraph* next = nullptr;
    EngineGraph* latest = nullptr;
    while (pending_.tryPop(next))
    {
        if (latest) retired_.push(&latest, 1); // superseded before it ever ran
        latest = next;
    }
    if (!latest) return;
    if (graph_) retired_.push(&graph_, 1);
    graph_ = latest;
    activeGeneration_.store(graph_->generation, std::memory_order_release);
    meters_.graphGeneration = graph_->generation;
}

void AudioEngine::tick(int numFrames) noexcept
{
    if (ticking_.test_and_set(std::memory_order_acquire))
    {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const int64_t t0 = monotonicNs();
    applyPendingGraph();

    int done = 0;
    while (done < numFrames)
    {
        const int n = std::min(kMaxBlock, numFrames - done);
        processBlock(n, clockFn_(clockCtx_));
        done += n;
    }

    const double elapsed = static_cast<double>(monotonicNs() - t0) * 1e-9;
    const double blockTime = static_cast<double>(numFrames) / sampleRate_;
    const double load = elapsed / blockTime;
    meters_.load = meters_.load * 0.95 + load * 0.05;
    peakAccum_ = std::max(peakAccum_, load);
    peakWindowFrames_ += numFrames;
    if (peakWindowFrames_ >= sampleRate_)
    {
        meters_.loadPeak = peakAccum_;
        peakAccum_ = 0.0;
        peakWindowFrames_ = 0;
    }
    ++meters_.ticks;
    meters_.skippedTicks = skipped_.load(std::memory_order_relaxed);
    meterSnapshot_.write(meters_);

    ticking_.clear(std::memory_order_release);
}

void AudioEngine::processBlock(int frames, int64_t nowNs) noexcept
{
    const EngineGraph* g = graph_;

    // 1. Pull every input bridge once (a multi-channel device can feed several channels).
    const size_t nIn = g ? std::min(g->inputs.size(), static_cast<size_t>(EngineGraph::kMaxInputBridges)) : 0;
    for (size_t b = 0; b < nIn; ++b)
        if (InputBridge* in = g->inputs[b].get())
            in->engineRead(&bridgePtrs_[b * EngineGraph::kMaxDeviceChannels], frames, nowNs);

    // 2. Channel inputs.
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        float* dst = channelBuffers_.data() + static_cast<size_t>(ch) * kMaxBlock;
        const ChannelRoute* r = g ? &g->channels[static_cast<size_t>(ch)] : nullptr;
        if (!r || r->inputBridge < 0 || static_cast<size_t>(r->inputBridge) >= nIn || !g->inputs[static_cast<size_t>(r->inputBridge)])
        {
            std::memset(dst, 0, sizeof(float) * static_cast<size_t>(frames));
        }
        else
        {
            const int devCh = std::min(g->inputs[static_cast<size_t>(r->inputBridge)]->config().deviceChannels,
                                       EngineGraph::kMaxDeviceChannels);
            float* const* src = &bridgePtrs_[static_cast<size_t>(r->inputBridge) * EngineGraph::kMaxDeviceChannels];
            if (r->inputChannel >= 0 && r->inputChannel < devCh)
            {
                std::memcpy(dst, src[r->inputChannel], sizeof(float) * static_cast<size_t>(frames));
            }
            else
            {
                const float scale = 1.0f / static_cast<float>(devCh);
                for (int i = 0; i < frames; ++i)
                {
                    float s = 0.0f;
                    for (int c = 0; c < devCh; ++c) s += src[c][i];
                    dst[i] = s * scale;
                }
            }
        }

        float pk = 0.0f;
        double sq = 0.0;
        for (int i = 0; i < frames; ++i)
        {
            const float a = std::abs(dst[i]);
            pk = std::max(pk, a);
            sq += static_cast<double>(dst[i]) * dst[i];
        }
        meters_.peak[static_cast<size_t>(ch)] = pk;
        meters_.rms[static_cast<size_t>(ch)] = static_cast<float>(std::sqrt(sq / frames));
    }

    if (EngineTap* tap = tap_.load(std::memory_order_acquire)) tap->onChannelBlock(channelPtrs_.data(), kNumChannels, frames);

    // 3. Routing: channels (post-DSP from Stage 5) → Main, Clean, Music, HP 1–8, PFL, Monitor.
    routing_.process(routingIn_, routingOut_, frames);
    for (size_t b = 0; b < kBusCount; ++b)
    {
        float pl = 0.0f, pr = 0.0f;
        const float* l = routingOut_.left[b];
        const float* r = routingOut_.right[b];
        for (int i = 0; i < frames; ++i)
        {
            pl = std::max(pl, std::abs(l[i]));
            pr = std::max(pr, std::abs(r[i]));
        }
        meters_.busPeak[b] = {pl, pr};
    }
    meters_.anySolo = routing_.anySolo();
    meters_.anyPfl = routing_.anyPfl();

    // 4. Outputs: each channel's headphone bus to its headphone endpoint, extra bus outputs.
    if (!g) return;
    const size_t nOut = std::min(g->outputs.size(), static_cast<size_t>(EngineGraph::kMaxOutputBridges));
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        const ChannelRoute& r = g->channels[static_cast<size_t>(ch)];
        if (r.outputBridge < 0 || static_cast<size_t>(r.outputBridge) >= nOut) continue;
        if (OutputBridge* out = g->outputs[static_cast<size_t>(r.outputBridge)].get())
        {
            const auto bus = static_cast<size_t>(hpBus(ch));
            out->engineWritePair(r.outputPair, routingOut_.left[bus], routingOut_.right[bus], frames);
        }
    }
    for (size_t b = 0; b < kBusCount; ++b)
    {
        const BusOutput& bo = g->busOutputs[b];
        if (bo.bridge < 0 || static_cast<size_t>(bo.bridge) >= nOut) continue;
        if (OutputBridge* out = g->outputs[static_cast<size_t>(bo.bridge)].get())
            out->engineWritePair(bo.pair, routingOut_.left[b], routingOut_.right[b], frames);
    }
    for (size_t b = 0; b < nOut; ++b)
        if (OutputBridge* out = g->outputs[b].get()) out->engineCommit(frames, nowNs);
}

} // namespace pf8
