#include "engine/AudioEngine.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

#include "core/Log.h"

namespace pf8 {

class AudioEngine::MasterCallback : public StreamCallback
{
public:
    explicit MasterCallback(AudioEngine& e) : engine_(e) {}

    void onStreamBlock(float* interleaved, int frames, int channels, int64_t, uint64_t) noexcept override
    {
        engine_.tick(frames);
        if (engine_.master_ && engine_.master_->config().flow == Flow::Render)
            engine_.renderInto(interleaved, frames, channels);
    }

    void onStreamError(StreamStatus s) noexcept override
    {
        PF8_LOG_WARN("engine", "master stream ended status=%s", toString(s));
    }

private:
    AudioEngine& engine_;
};

AudioEngine::AudioEngine(int sampleRate, int blockFrames) : sampleRate_(sampleRate), blockFrames_(blockFrames)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpcFrequency_ = f.QuadPart;
}

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start(const std::optional<StreamConfig>& master, const std::string& masterName, std::string& error)
{
    stop();
    rt_ = {};
    peakAccum_ = 0.0;
    peakWindowFrames_ = 0.0;

    Status cfg;
    cfg.backend = "WASAPI";
    cfg.requestedFrames = blockFrames_;

    if (master)
    {
        masterCallback_ = std::make_unique<MasterCallback>(*this);
        master_ = std::make_unique<WasapiStream>(*master, masterCallback_.get());
        if (master_->open(error) && master_->start())
        {
            cfg.masterId = master->endpointId;
            cfg.masterName = masterName;
            cfg.mode = master_->grantedMode();
            cfg.sampleFormat = master_->sampleFormatName();
            cfg.sampleRate = master_->sampleRate();
            cfg.grantedPeriod = master_->periodFrames();
            cfg.running = true;
            PF8_LOG_INFO("engine", "engine.start master=%s mode=%s rate=%d period=%d requested=%d",
                         cfg.masterId.c_str(), toString(cfg.mode), cfg.sampleRate, cfg.grantedPeriod, blockFrames_);
        }
        else
        {
            PF8_LOG_WARN("engine", "master open failed id=%s: %s — using internal clock", master->endpointId.c_str(),
                         error.c_str());
            master_.reset();
            masterCallback_.reset();
        }
    }

    if (!master_)
    {
        internal_ = std::make_unique<InternalClock>(this, sampleRate_, blockFrames_);
        internal_->start();
        cfg.internalClock = true;
        cfg.sampleRate = sampleRate_;
        cfg.grantedPeriod = blockFrames_;
        cfg.sampleFormat = "float32";
        cfg.running = true;
        PF8_LOG_INFO("engine", "engine.start internal clock rate=%d block=%d", sampleRate_, blockFrames_);
    }

    std::lock_guard lock(statusMutex_);
    config_ = cfg;
    return cfg.running;
}

void AudioEngine::stop()
{
    if (master_) master_->stop();
    if (internal_) internal_->stop();
    const bool wasRunning = master_ || internal_;
    master_.reset();
    masterCallback_.reset();
    internal_.reset();
    {
        std::lock_guard lock(statusMutex_);
        config_.running = false;
    }
    if (wasRunning) PF8_LOG_INFO("engine", "engine.stop");
}

AudioEngine::Status AudioEngine::status() const
{
    Status s;
    {
        std::lock_guard lock(statusMutex_);
        s = config_;
    }
    const RtStats r = rtSnapshot_.read();
    s.load = r.load;
    s.loadPeak = r.loadPeak;
    s.ticks = r.ticks;
    if (master_)
    {
        s.masterStatus = master_->status();
        s.glitches = master_->stats().glitches.load();
        if (s.masterStatus != StreamStatus::Running) s.running = false;
    }
    else if (internal_)
    {
        s.masterStatus = StreamStatus::Running;
    }
    return s;
}

void AudioEngine::tick(int numFrames) noexcept
{
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);

    // Stage 1: nothing to process yet. Later stages run sources → DSP → routing → outputs here,
    // in sub-blocks of at most kMaxBlock frames.

    QueryPerformanceCounter(&t1);
    const double elapsed = static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(qpcFrequency_);
    const double blockTime = static_cast<double>(numFrames) / (sampleRate_ > 0 ? sampleRate_ : 48000);
    const double load = blockTime > 0 ? elapsed / blockTime : 0.0;
    rt_.load = rt_.load * 0.95 + load * 0.05;
    peakAccum_ = std::max(peakAccum_, load);
    peakWindowFrames_ += numFrames;
    if (peakWindowFrames_ >= sampleRate_)
    {
        rt_.loadPeak = peakAccum_;
        peakAccum_ = 0.0;
        peakWindowFrames_ = 0.0;
    }
    ++rt_.ticks;
    rtSnapshot_.write(rt_);
}

void AudioEngine::renderInto(float* interleaved, int frames, int channels) noexcept
{
    // Stage 1: silence. (The stream pre-zeroes the buffer; kept explicit for clarity.)
    std::memset(interleaved, 0, sizeof(float) * static_cast<size_t>(frames) * static_cast<size_t>(channels));
}

} // namespace pf8
