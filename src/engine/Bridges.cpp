#include "engine/Bridges.h"

#include "engine/DriftController.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pf8 {
namespace {

constexpr double kFadeSeconds = 0.005;

int deviceFramesFor(int engineFrames, const BridgeConfig& c) noexcept
{
    return static_cast<int>(std::lround(static_cast<double>(engineFrames) * c.deviceRate / c.engineRate));
}

} // namespace

int bridgeTargetFill(const BridgeConfig& c) noexcept
{
    if (c.master) return 0;
    const int block = deviceFramesFor(std::max(c.engineBlock, c.engineBurst), c);
    const int hi = std::max(c.devicePeriod, block), lo = std::min(c.devicePeriod, block);
    const int jitter = c.deviceRate / 500; // 2 ms
    return (3 * hi + lo) / 2 + jitter + std::max(0, c.deviceBufferFrames);
}

BridgeStats BridgeCounters::read() const noexcept
{
    BridgeStats s;
    s.underruns = underruns_.load(std::memory_order_relaxed);
    s.overruns = overruns_.load(std::memory_order_relaxed);
    s.droppedFrames = dropped_.load(std::memory_order_relaxed);
    s.ppm = ppm_.load(std::memory_order_relaxed);
    s.fill = fill_.load(std::memory_order_relaxed);
    s.target = target_;
    s.status = status_.load(std::memory_order_relaxed);
    s.timestampAgeMs = tsAgeMs_.load(std::memory_order_relaxed);
    s.deviceFrames = deviceFrames_.load(std::memory_order_relaxed);
    s.recentres = recentres_.load(std::memory_order_relaxed);
    s.measuredNow = measuredNow_.load(std::memory_order_relaxed);
    s.ringNow = ringNow_.load(std::memory_order_relaxed);
    s.clockOffsetUs = clockOffsetUs_.load(std::memory_order_relaxed);
    return s;
}

// ---------------------------------------------------------------------------------------------
// InputBridge

InputBridge::InputBridge(const BridgeConfig& config)
    : cfg_(config),
      ring_(static_cast<size_t>(4 * bridgeTargetFill(config) + 4 * config.devicePeriod + 8192) *
            static_cast<size_t>(config.deviceChannels))
{
    const int ch = cfg_.deviceChannels;
    // Live shared-mode capture delivers each packet up to one period after its last frame was
    // captured; the linearised measure counts that, so the ring itself runs up to a period lower.
    target_ = bridgeTargetFill(cfg_) + (cfg_.master ? 0 : cfg_.devicePeriod / 2);
    rs_.prepare(ch, static_cast<double>(cfg_.deviceRate) / cfg_.engineRate, kMaxBlock, cfg_.master);
    dc_.prepare(cfg_.deviceRate, target_, 0.5 * deviceFramesFor(cfg_.engineBlock, cfg_));
    const int maxIn = static_cast<int>(std::ceil(kMaxBlock * static_cast<double>(cfg_.deviceRate) / cfg_.engineRate * 1.01)) +
                      VarResampler::kTaps + 16;
    staging_.assign(static_cast<size_t>(maxIn) * ch, 0.0f);
    resampled_.assign(static_cast<size_t>(kMaxBlock) * ch, 0.0f);
    fadeLength_ = std::max(1, static_cast<int>(kFadeSeconds * cfg_.engineRate));
    status_ = cfg_.master ? SyncStatus::Native : SyncStatus::Priming;
}

void InputBridge::deviceWrite(const float* interleaved, int frames, int64_t callbackNs) noexcept
{
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(cfg_.deviceChannels);
    const size_t written = ring_.push(interleaved, samples);
    if (written < samples)
    {
        overruns_.fetch_add(1, std::memory_order_relaxed);
        dropped_.fetch_add((samples - written) / static_cast<size_t>(cfg_.deviceChannels), std::memory_order_relaxed);
    }
    if (!dll_.started()) dll_.reset(cfg_.deviceRate, cfg_.devicePeriod, DriftController::tuning().dllBandwidthHz);
    // The filtered callback time stands for "the device has produced everything delivered so far".
    lastEndNs_.store(dll_.update(callbackNs, frames), std::memory_order_release);
    deviceFrames_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_relaxed);
}

void InputBridge::driveEngine(TickClient& engine, int64_t) noexcept
{
    // deviceWrite() just updated this device's DLL: that filtered time is the engine's "now".
    const int64_t engineNow = lastEndNs_.load(std::memory_order_acquire);
    // Only valid for a master bridge: the engine side runs on this (device) thread.
    for (int guard = 0; guard < 64; ++guard)
    {
        const int need = rs_.inputFramesNeeded(cfg_.engineBlock);
        if (static_cast<int>(readableDeviceFrames()) < need) break;
        if (engineNow != 0) engine.setTickTimeNs(engineNow);
        engine.tick(cfg_.engineBlock);
    }
}

void InputBridge::outputSilence(float* const* perChannel, int frames) noexcept
{
    for (int c = 0; c < cfg_.deviceChannels; ++c)
        std::memset(perChannel[c], 0, sizeof(float) * static_cast<size_t>(frames));
}

void InputBridge::engineRead(float* const* perChannel, int frames, int64_t nowNs) noexcept
{
    const int ch = cfg_.deviceChannels;
    double ringFrames = static_cast<double>(readableDeviceFrames());

    // Linearised fill: frames the device has captured since the end of its last delivered packet
    // (hardware time) count as present, so neither packet size nor callback jitter aliases in.
    // Priming, re-centring and the drift loop all use this same measure.
    auto measure = [&] {
        double m = static_cast<double>(readableDeviceFrames());
        const int64_t lastEnd = lastEndNs_.load(std::memory_order_acquire);
        if (lastEnd > 0)
        {
            tsAgeMs_.store(static_cast<double>(nowNs - lastEnd) * 1e-6, std::memory_order_relaxed);
            const double maxSpan = 4.0 * cfg_.devicePeriod / cfg_.deviceRate;
            m += std::clamp(static_cast<double>(nowNs - lastEnd) * 1e-9, -maxSpan, maxSpan) * cfg_.deviceRate;
        }
        return m;
    };

    if (!running_)
    {
        const double measured = measure();
        const int need = rs_.inputFramesNeeded(frames);
        if (ringFrames > 0 && measured >= target_ && ringFrames >= need)
        {
            // Start centred on the target: a device that ran before its bridge joined the graph
            // (or during re-priming) would otherwise begin with excess latency that the drift loop
            // could only remove slowly. Safe to drop: the output fades in from zero.
            if (!cfg_.master)
            {
                const double excess = std::min(measured - target_, ringFrames - need);
                if (excess > cfg_.devicePeriod / 4.0)
                {
                    ring_.discard(static_cast<size_t>(excess) * static_cast<size_t>(ch));
                    ringFrames -= std::floor(excess);
                }
            }
            running_ = true;
            rs_.reset();
            fadeRemaining_ = fadeLength_;
            dc_.reset(dc_.ppm());
            settleFramesLeft_ = static_cast<int64_t>(kSettleSeconds * cfg_.engineRate);
            if (!cfg_.master) status_ = SyncStatus::Converging;
        }
        else
        {
            if (!cfg_.master) status_ = SyncStatus::Priming;
            outputSilence(perChannel, frames);
            return;
        }
    }

    // Engine stalled or device burst: discard excess instead of carrying huge latency.
    if (!cfg_.master && ringFrames > 4.0 * target_ + cfg_.devicePeriod)
    {
        const size_t excess = static_cast<size_t>(ringFrames - target_) * static_cast<size_t>(ch);
        ring_.discard(excess);
        overruns_.fetch_add(1, std::memory_order_relaxed);
        dropped_.fetch_add(excess / static_cast<size_t>(ch), std::memory_order_relaxed);
        dc_.reset(dc_.ppm());
    }

    if (silenceFrames_ > 0)
    {
        // Re-centre deficit: hold back device audio for a few ms so the ring refills (fades back in).
        silenceFrames_ = std::max(0, silenceFrames_ - frames);
        outputSilence(perChannel, frames);
        if (silenceFrames_ == 0) fadeRemaining_ = fadeLength_;
        return;
    }

    const int need = rs_.inputFramesNeeded(frames);
    const int avail = static_cast<int>(readableDeviceFrames());
    if (avail < need)
    {
        underruns_.fetch_add(1, std::memory_order_relaxed);
        running_ = false;
        if (!cfg_.master) status_ = SyncStatus::Priming;
        outputSilence(perChannel, frames);
        return;
    }
    const double measured = measure();

    int remaining = need;
    while (remaining > 0)
    {
        const int chunk = std::min(remaining, static_cast<int>(staging_.size() / static_cast<size_t>(ch)));
        const size_t got = ring_.pop(staging_.data(), static_cast<size_t>(chunk) * ch) / static_cast<size_t>(ch);
        rs_.pushInput(staging_.data(), static_cast<int>(got));
        remaining -= static_cast<int>(got);
        if (got == 0) break;
    }
    const int produced = rs_.process(resampled_.data(), frames);

    for (int i = 0; i < frames; ++i)
    {
        float g = 1.0f;
        if (fadeRemaining_ > 0)
        {
            g = 1.0f - static_cast<float>(fadeRemaining_) / static_cast<float>(fadeLength_);
            --fadeRemaining_;
        }
        for (int c = 0; c < ch; ++c)
            perChannel[c][i] = i < produced ? resampled_[static_cast<size_t>(i) * ch + c] * g : 0.0f;
    }

    measuredNow_.store(measured, std::memory_order_relaxed);
    ringNow_.store(static_cast<double>(avail), std::memory_order_relaxed);
    if (!cfg_.master)
    {
        const double ppm = dc_.update(measured, deviceFramesFor(frames, cfg_));
        if (settleFramesLeft_ > 0 && (settleFramesLeft_ -= frames) <= 0)
        {
            const double e = dc_.averagedFill() - target_;
            if (std::abs(e) > cfg_.devicePeriod / 4.0)
            {
                if (e > 0)
                {
                    const size_t drop = std::min(static_cast<size_t>(e), readableDeviceFrames());
                    ring_.discard(drop * static_cast<size_t>(ch));
                    fadeRemaining_ = fadeLength_;
                }
                else
                    silenceFrames_ = deviceFramesFor(static_cast<int>(-e), cfg_);
                dc_.reset(0.0);
                recentres_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        rs_.setRatioCorrectionPpm(dc_.ppm());
        ppm_.store(ppm, std::memory_order_relaxed);
        fill_.store(dc_.averagedFill(), std::memory_order_relaxed);
        status_.store(dc_.status(), std::memory_order_relaxed);
    }
    else
    {
        fill_.store(static_cast<double>(readableDeviceFrames()), std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------------------------
// OutputBridge

OutputBridge::OutputBridge(const BridgeConfig& config, int pairs)
    : cfg_(config),
      pairs_(std::max(1, pairs)),
      ring_(static_cast<size_t>(4 * bridgeTargetFill(config) + 4 * config.devicePeriod + 8192) *
            static_cast<size_t>(config.deviceChannels))
{
    target_ = bridgeTargetFill(cfg_);
    const int rsCh = 2 * pairs_;
    const double ratio = static_cast<double>(cfg_.engineRate) / cfg_.deviceRate;
    const int maxOut = static_cast<int>(std::ceil(kMaxBlock / ratio * 1.01)) + 8;
    rs_.prepare(rsCh, ratio, maxOut, cfg_.master);
    dc_.prepare(cfg_.deviceRate, target_, 0.5 * deviceFramesFor(cfg_.engineBlock, cfg_));
    pairStaging_.assign(static_cast<size_t>(kMaxBlock) * rsCh, 0.0f);
    resampled_.assign(static_cast<size_t>(maxOut) * rsCh, 0.0f);
    deviceFrames_.assign(static_cast<size_t>(maxOut) * cfg_.deviceChannels, 0.0f);
    fadeLength_ = std::max(1, static_cast<int>(kFadeSeconds * cfg_.deviceRate));
    status_ = cfg_.master ? SyncStatus::Native : SyncStatus::Priming;
}

void OutputBridge::engineWritePair(int pair, const float* left, const float* right, int frames) noexcept
{
    if (pair < 0 || pair >= pairs_) return;
    const int stride = 2 * pairs_;
    float* dst = pairStaging_.data() + 2 * pair;
    for (int i = 0; i < frames; ++i)
    {
        dst[static_cast<size_t>(i) * stride] = left[i];
        dst[static_cast<size_t>(i) * stride + 1] = right[i];
    }
}

void OutputBridge::engineCommit(int frames, int64_t nowNs) noexcept
{
    const int rsCh = 2 * pairs_;
    const int devCh = cfg_.deviceChannels;
    rs_.pushInput(pairStaging_.data(), frames);
    const int maxOut = static_cast<int>(resampled_.size() / static_cast<size_t>(rsCh));
    const int produced = rs_.process(resampled_.data(), maxOut);
    std::fill(pairStaging_.begin(), pairStaging_.begin() + static_cast<ptrdiff_t>(frames) * rsCh, 0.0f);

    for (int i = 0; i < produced; ++i)
    {
        const float* src = resampled_.data() + static_cast<size_t>(i) * rsCh;
        float* dst = deviceFrames_.data() + static_cast<size_t>(i) * devCh;
        if (devCh == 1)
            dst[0] = 0.5f * (src[0] + src[1]);
        else
            for (int c = 0; c < devCh; ++c) dst[c] = (c / 2 < pairs_) ? src[c] : 0.0f;
    }
    size_t offsetFrames = 0;
    if (skipFrames_ > 0)
    {
        offsetFrames = static_cast<size_t>(std::min<int64_t>(skipFrames_, produced));
        skipFrames_ -= static_cast<int64_t>(offsetFrames);
    }
    const size_t samples = (static_cast<size_t>(produced) - offsetFrames) * devCh;
    const size_t written = ring_.push(deviceFrames_.data() + offsetFrames * devCh, samples);
    totalWritten_ += written / static_cast<size_t>(devCh);
    if (written < samples)
    {
        overruns_.fetch_add(1, std::memory_order_relaxed);
        dropped_.fetch_add((samples - written) / static_cast<size_t>(devCh), std::memory_order_relaxed);
    }

    if (!cfg_.master)
    {
        if (devicePriming_.load(std::memory_order_acquire))
        {
            status_.store(SyncStatus::Priming, std::memory_order_relaxed);
            fill_.store(static_cast<double>(readableDeviceFrames()), std::memory_order_relaxed);
            settleFramesLeft_ = static_cast<int64_t>(kSettleSeconds * cfg_.engineRate);
            return;
        }
        // Linearised fill against hardware playback: frames written minus frames the device has
        // consumed by now (the last packet's start plus elapsed time at the device rate).
        const ReadPos rp = readPos_.read();
        if (rp.startNs == 0) return;
        const double maxSpan = 4.0 * cfg_.devicePeriod / cfg_.deviceRate;
        const double elapsed = std::clamp(static_cast<double>(nowNs - rp.startNs) * 1e-9, -maxSpan, maxSpan);
        const double consumed = static_cast<double>(rp.ringRead) + elapsed * cfg_.deviceRate;
        const double measured = static_cast<double>(totalWritten_) - consumed;
        const double ppm = dc_.update(measured, deviceFramesFor(frames, cfg_));
        if (settleFramesLeft_ > 0 && (settleFramesLeft_ -= frames) <= 0)
        {
            const double e = dc_.averagedFill() - target_;
            if (std::abs(e) > cfg_.devicePeriod / 4.0)
            {
                if (e > 0) skipFrames_ = static_cast<int64_t>(e); // produce less for a moment
                else
                {
                    // Deficit: insert silence frames into the ring.
                    const size_t n = static_cast<size_t>(-e) * static_cast<size_t>(devCh);
                    std::fill(deviceFrames_.begin(), deviceFrames_.end(), 0.0f);
                    size_t left = n;
                    while (left > 0)
                    {
                        const size_t c = std::min(left, deviceFrames_.size());
                        const size_t w = ring_.push(deviceFrames_.data(), c);
                        totalWritten_ += w / static_cast<size_t>(devCh);
                        if (w < c) break;
                        left -= c;
                    }
                }
                dc_.reset(0.0);
                recentres_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        rs_.setRatioCorrectionPpm(dc_.ppm());
        ppm_.store(ppm, std::memory_order_relaxed);
        fill_.store(dc_.averagedFill(), std::memory_order_relaxed);
        status_.store(dc_.status(), std::memory_order_relaxed);
    }
    else
    {
        fill_.store(static_cast<double>(readableDeviceFrames()), std::memory_order_relaxed);
    }
}

void OutputBridge::deviceRead(float* interleaved, int frames, int64_t nowNs, TickClient* driver) noexcept
{
    const int devCh = cfg_.deviceChannels;
    if (!dll_.started()) dll_.reset(cfg_.deviceRate, cfg_.devicePeriod, DriftController::tuning().dllBandwidthHz);
    const int64_t deviceNow = dll_.update(nowNs, frames);
    if (driver)
    {
        // Every tick of this burst happens "now" on the master's clock: the DLL removes the
        // callback scheduling jitter while following the master's true rate.
        const int64_t engineNow = deviceNow;
        clockOffsetUs_.store(static_cast<double>(engineNow - nowNs) * 1e-3, std::memory_order_relaxed);
        const int maxTicks = frames / std::max(1, deviceFramesFor(cfg_.engineBlock, cfg_)) + 8;
        for (int t = 0; t < maxTicks && static_cast<int>(readableDeviceFrames()) < frames; ++t)
        {
            driver->setTickTimeNs(engineNow);
            driver->tick(cfg_.engineBlock);
        }
    }

    if (devicePriming_.load(std::memory_order_relaxed))
    {
        // Same measure the engine side regulates: ring plus what the device still holds ahead of
        // playback (packetStart lies in the future by the device's own buffering).
        const double avail = static_cast<double>(readableDeviceFrames());
        const double measured = avail;
        if (avail >= frames && measured >= target_)
        {
            // Start centred on the target (see InputBridge): drop excess queued while priming.
            if (!cfg_.master)
            {
                const double excess = std::min(measured - target_, avail - frames);
                if (excess > cfg_.devicePeriod / 4.0)
                {
                    const size_t n = static_cast<size_t>(excess);
                    ring_.discard(n * static_cast<size_t>(devCh));
                    totalRead_ += n;
                }
            }
            devicePriming_.store(false, std::memory_order_release);
            fadeRemaining_ = fadeLength_;
        }
        else
        {
            std::memset(interleaved, 0, sizeof(float) * static_cast<size_t>(frames) * devCh);
            return;
        }
    }

    readPos_.write(ReadPos{totalRead_, deviceNow});
    const size_t want = static_cast<size_t>(frames) * devCh;
    const size_t got = ring_.pop(interleaved, want);
    totalRead_ += got / static_cast<size_t>(devCh);
    if (got < want)
    {
        std::memset(interleaved + got, 0, sizeof(float) * (want - got));
        underruns_.fetch_add(1, std::memory_order_relaxed);
        devicePriming_.store(true, std::memory_order_release);
    }
    for (int i = 0; i < frames && fadeRemaining_ > 0; ++i, --fadeRemaining_)
    {
        const float g = 1.0f - static_cast<float>(fadeRemaining_) / static_cast<float>(fadeLength_);
        for (int c = 0; c < devCh; ++c) interleaved[static_cast<size_t>(i) * devCh + c] *= g;
    }
}

} // namespace pf8
