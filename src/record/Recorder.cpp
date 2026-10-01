#include "record/Recorder.h"

#include <algorithm>

#include "core/Paths.h"
#include "core/Log.h"
#include "record/Session.h"

namespace pf8 {

namespace {
constexpr auto kCheckpoint = std::chrono::seconds(2);
constexpr auto kRetry = std::chrono::seconds(1);
constexpr uint64_t kMaxPendingPerTrack = 200ull << 20; // stop draining (tick then counts dropouts)

std::string relativePathString(const std::filesystem::path& base, const std::filesystem::path& p)
{
    std::error_code ec;
    auto r = std::filesystem::relative(p, base, ec);
    return paths::utf8Generic(ec ? p : r);
}
} // namespace

const char* toString(Recorder::State s) noexcept
{
    switch (s)
    {
        case Recorder::State::Idle: return "STOPPED";
        case Recorder::State::Recording: return "RECORDING";
        case Recorder::State::Paused: return "PAUSED";
    }
    return "?";
}

Recorder::Recorder(RecordTap& tap, int sampleRate, std::unique_ptr<SinkFactory> factory)
    : tap_(tap), rate_(sampleRate), factory_(std::move(factory))
{
    markers_.setSampleRate(sampleRate);
}

Recorder::~Recorder() { stop(); }

double Recorder::bytesPerSecond(const Settings& s) const
{
    int mono = 0;
    for (bool a : s.armed) mono += a ? 1 : 0;
    const int bps = s.format == FileFormat::Flac ? 2 : bytesPerSample(s.depth); // FLAC ~ half of 24-bit PCM
    return DiskGuard::bytesPerSecond(mono, (s.recordMain ? 1 : 0) + (s.recordMusic ? 1 : 0), rate_, bps);
}

std::filesystem::path Recorder::metadataDir() const
{
    std::lock_guard lock(mutex_);
    return session_.empty() ? std::filesystem::path() : session_ / "Metadata";
}

bool Recorder::openTrack(Track& t, const std::filesystem::path& file, const std::string& name, std::string& error)
{
    TrackFileInfo info;
    info.path = file;
    info.format = settings_.format;
    info.depth = settings_.depth;
    info.channels = t.channels;
    info.sampleRate = rate_;
    info.trackName = name;
    info.description = settings_.description;
    const auto stamp = localStamp(rate_);
    info.originationDate = stamp.date;
    info.originationTime = stamp.time;
    info.timeReference = stamp.samplesSinceMidnight;
    t.writer = settings_.format == FileFormat::Flac ? makeTrackWriter(FileFormat::Flac)
                                                    : std::make_unique<WavWriter>(settings_.format == FileFormat::Bwf, settings_.rf64Threshold);
    const auto e = t.writer->open(info, factory_->make());
    if (e != SinkError::None)
    {
        error = "cannot create " + paths::utf8(file) + ": " + toString(e);
        return false;
    }
    t.journal.file = relativePathString(t.relativeTo, file);
    t.journal.name = name;
    t.journal.format = settings_.format;
    t.journal.bits = static_cast<int>(t.writer->info().depth);
    t.journal.channels = t.channels;
    t.journal.headerBytes = t.writer->headerBytes();
    t.journal.blockAlign = t.writer->blockAlign();
    t.journal.samplesWritten = 0;
    t.journal.state = "recording";
    return true;
}

bool Recorder::start(const Settings& settings, std::string& error)
{
    if (state_.load() != State::Idle)
    {
        error = "already recording";
        return false;
    }
    settings_ = settings;
    auto dir = createSessionFolder(settings.projectDir, &error);
    if (!dir) return false;

    std::vector<Track> tracks;
    uint32_t mask = 0;
    for (int ch = 0; ch < 8; ++ch)
    {
        if (!settings.armed[static_cast<size_t>(ch)]) continue;
        Track t;
        t.id = ch;
        t.channels = 1;
        t.relativeTo = *dir;
        char stem[16];
        std::snprintf(stem, sizeof stem, "CH%02d_", ch + 1);
        const std::string name = settings.names[static_cast<size_t>(ch)].empty() ? "Channel " + std::to_string(ch + 1)
                                                                                  : settings.names[static_cast<size_t>(ch)];
        const auto file = uniqueFilePath(*dir / "Audio", stem + sanitizeFileName(name), extension(settings.format));
        if (!openTrack(t, file, name, error)) return false;
        mask |= 1u << ch;
        tracks.push_back(std::move(t));
    }
    if (settings.recordMain)
    {
        Track t;
        t.id = static_cast<int>(TrackId::Main);
        t.channels = 2;
        t.relativeTo = *dir;
        const auto file = uniqueFilePath(*dir / "Mix", "MainMix", extension(settings.format));
        if (!openTrack(t, file, "Main Mix", error)) return false;
        mask |= 1u << static_cast<int>(TrackId::Main);
        tracks.push_back(std::move(t));
    }
    if (settings.recordMusic)
    {
        Track t;
        t.id = static_cast<int>(TrackId::Music);
        t.channels = 2;
        t.relativeTo = *dir;
        const auto file = uniqueFilePath(*dir / "Audio", "Music", extension(settings.format));
        if (!openTrack(t, file, "Music", error)) return false;
        mask |= 1u << static_cast<int>(TrackId::Music);
        tracks.push_back(std::move(t));
    }
    if (tracks.empty())
    {
        error = "no track is armed";
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        tracks_ = std::move(tracks);
        session_ = *dir;
        journalPath_ = *dir / "Metadata" / "Journal.json";
        journal_ = Journal{};
        journal_.sampleRate = rate_;
        journal_.startedUtc = journal_.updatedUtc = utcNowIso();
        journal_.tracks.clear();
        for (const auto& t : tracks_) journal_.tracks.push_back(t.journal);
        writeError_ = false;
        writeErrorText_.clear();
        continueRequest_.reset();
        writeRate_ = 0.0;
        lastBytes_ = 0;
        lastRateTime_ = std::chrono::steady_clock::now();
        drainBuffer_.assign(static_cast<size_t>(rate_) * 2, 0.0f);
        markers_.clear();
    }

    // The tap ring must also hold the live audio that arrives while the worker writes the pre-roll.
    double ring = settings.ringSeconds;
    if (prerollSource_ && prerollSource_->capacity() > 0) ring += std::min(20.0, 4.0 + prerollSource_->seconds() / 4.0);
    tap_.configure(mask, rate_, ring);
    tap_.setPaused(false);
    stopRequested_ = false;
    prerollFrames_ = 0;
    prerollActive_ = prerollSource_ && prerollSource_->capacity() > 0 ? prerollSource_ : nullptr;
    if (prerollActive_) prerollActive_->armCapture();
    state_ = State::Recording;
    tap_.setActive(true);
    // Pre-roll: the tick freezes it on the first recorded block (sample-contiguous with the live
    // stream). Wait for that block so marker positions and the journal are right from the start.
    if (prerollActive_)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!prerollActive_->frozen() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (prerollActive_->frozen()) prerollFrames_ = prerollActive_->frozenFrames();
        else PF8_LOG_WARN("record", "pre-roll: the engine did not tick within 500 ms - recording without pre-roll");
    }
    {
        std::lock_guard lock(mutex_);
        journal_.prerollSamples = prerollFrames_;
        journal_.save(journalPath_);
        if (prerollFrames_ > 0) markers_.add(prerollFrames_, "Record pressed");
    }
    worker_ = std::thread([this] { workerMain(); });
    PF8_LOG_INFO("record", "record.start session=%s tracks=%zu format=%s bits=%d", paths::utf8(session_).c_str(), tracks_.size(),
                 toString(settings.format), static_cast<int>(settings.depth));
    return true;
}

void Recorder::writePreroll()
{
    const uint64_t total = prerollFrames_.load();
    if (!prerollActive_ || total == 0) return;
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        const int chunk = static_cast<int>(drainBuffer_.size() / static_cast<size_t>(t.channels));
        for (uint64_t done = 0; done < total;)
        {
            const int n = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(chunk), total - done));
            prerollActive_->read(t.id, done, n, drainBuffer_.data());
            if (const auto e = t.writer->write(drainBuffer_.data(), n); e != SinkError::None) noteError(e, t.journal.file);
            done += static_cast<uint64_t>(n);
        }
    }
    PF8_LOG_INFO("record", "pre-roll written frames=%llu", static_cast<unsigned long long>(total));
}

void Recorder::pause()
{
    if (state_.load() != State::Recording) return;
    tap_.setPaused(true);
    state_ = State::Paused;
    addMarker("Pause");
    PF8_LOG_INFO("record", "record.pause");
}

void Recorder::resume()
{
    if (state_.load() != State::Paused) return;
    addMarker("Resume");
    tap_.setPaused(false);
    state_ = State::Recording;
    PF8_LOG_INFO("record", "record.resume");
}

int Recorder::addMarker(const std::string& label)
{
    if (state_.load() == State::Idle) return 0;
    const uint64_t pos = prerollFrames_ + tap_.framesPushed();
    const int id = markers_.add(pos, label);
    markers_.save(metadataDir()); // immediately, so a crash keeps it
    PF8_LOG_INFO("record", "marker id=%d pos=%llu", id, static_cast<unsigned long long>(pos));
    return id;
}

void Recorder::stop()
{
    if (state_.load() == State::Idle && !worker_.joinable()) return;
    tap_.setActive(false);
    stopRequested_ = true;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    state_ = State::Idle;
    prerollFrames_ = 0;
    if (prerollActive_) prerollActive_->release();
    prerollActive_ = nullptr;
}

void Recorder::noteError(SinkError e, const std::string& what)
{
    if (!writeError_)
    {
        writeError_ = true;
        errorSince_ = std::chrono::steady_clock::now();
        PF8_LOG_ERROR("record", "write error on %s: %s - audio kept in memory, retrying", what.c_str(), toString(e));
    }
    writeErrorText_ = what + ": " + toString(e);
}

void Recorder::drainOnce(bool final)
{
    std::lock_guard lock(mutex_);
    for (auto& t : tracks_)
    {
        auto* ring = tap_.ring(t.id);
        if (!ring) continue;
        for (;;)
        {
            // Leave audio in the ring rather than pop what can't be stored (the tick then counts a
            // visible dropout instead of silently losing it here).
            if (!final && t.writer->pendingBytes() > kMaxPendingPerTrack) break;
            const size_t maxSamples = drainBuffer_.size() - drainBuffer_.size() % static_cast<size_t>(t.channels);
            const size_t got = ring->pop(drainBuffer_.data(), maxSamples);
            if (got == 0) break;
            const int frames = static_cast<int>(got / static_cast<size_t>(t.channels));
            const auto e = t.writer->write(drainBuffer_.data(), frames);
            if (e != SinkError::None) noteError(e, t.journal.file);
        }
    }
}

void Recorder::checkpoint()
{
    std::lock_guard lock(mutex_);
    uint64_t bytes = 0;
    for (auto& t : tracks_)
    {
        if (!writeError_)
            if (const auto e = t.writer->updateHeader(); e != SinkError::None) noteError(e, t.journal.file);
        t.journal.samplesWritten = t.writer->framesWritten();
        bytes += t.writer->bytesOnDisk();
    }
    journal_.tracks.clear();
    for (const auto& t : tracks_) journal_.tracks.push_back(t.journal);
    journal_.state = state_.load() == State::Paused ? "paused" : "recording";
    journal_.updatedUtc = utcNowIso();
    journal_.droppedFrames = tap_.framesDropped();
    journal_.save(journalPath_);

    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - lastRateTime_).count();
    if (dt > 0.1) writeRate_ = static_cast<double>(bytes - std::min(bytes, lastBytes_)) / dt;
    lastBytes_ = bytes;
    lastRateTime_ = now;
    int mono = 0, stereo = 0;
    for (const auto& t : tracks_) (t.channels == 2 ? stereo : mono)++;
    const int bps = settings_.format == FileFormat::Flac ? 2 : bytesPerSample(settings_.depth);
    disk_ = DiskGuard::estimate(session_, DiskGuard::bytesPerSecond(mono, stereo, rate_, bps));
    if (disk_.level == DiskLevel::Red) PF8_LOG_WARN("record", "disk critically low: %.0f s left", disk_.secondsRemaining);
}

void Recorder::retryErrors()
{
    std::lock_guard lock(mutex_);
    if (!writeError_) return;
    bool allOk = true;
    for (auto& t : tracks_)
    {
        const auto e = t.writer->retryPending();
        if (e != SinkError::None || t.writer->pendingBytes() > 0)
        {
            allOk = false;
            if (e != SinkError::None) writeErrorText_ = t.journal.file + ": " + toString(e);
        }
    }
    if (allOk)
    {
        writeError_ = false;
        writeErrorText_.clear();
        PF8_LOG_INFO("record", "writes recovered - all pending audio is on disk");
    }
}

void Recorder::performContinue()
{
    std::lock_guard lock(mutex_);
    if (!continueRequest_) return;
    const auto base = *continueRequest_;
    continueRequest_.reset();
    const auto name = paths::utf8(session_.filename()) + "_part2";
    auto dir = base / name;
    std::error_code ec;
    std::filesystem::create_directories(dir / "Audio", ec);
    std::filesystem::create_directories(dir / "Mix", ec);
    std::filesystem::create_directories(dir / "Metadata", ec);
    if (ec)
    {
        continueError_ = "cannot create " + paths::utf8(dir);
        continueDone_ = true;
        return;
    }
    // Old files: finalise to what reached the disk (best effort — the disk may still be full).
    std::vector<Track> next;
    for (auto& t : tracks_)
    {
        auto pending = t.writer->takePending();
        t.writer->finalise({});
        t.journal.state = "finalised";
        t.journal.samplesWritten = t.writer->framesWritten();
        Track n;
        n.id = t.id;
        n.channels = t.channels;
        n.relativeTo = dir;
        const auto sub = t.id == static_cast<int>(TrackId::Main) ? "Mix" : "Audio";
        const auto file = uniqueFilePath(dir / sub, paths::utf8(paths::fromUtf8(t.journal.file).stem()) + "_part2",
                                         extension(settings_.format));
        std::string err;
        if (!openTrack(n, file, t.journal.name, err))
        {
            continueError_ = err;
            continueDone_ = true;
            return;
        }
        n.writer->appendRaw(pending);
        next.push_back(std::move(n));
    }
    journal_.state = "continued";
    journal_.tracks.clear();
    for (const auto& t : tracks_) journal_.tracks.push_back(t.journal);
    journal_.save(journalPath_); // best effort on the full disk

    tracks_ = std::move(next);
    session_ = dir;
    journalPath_ = dir / "Metadata" / "Journal.json";
    journal_.state = "recording";
    journal_.tracks.clear();
    for (const auto& t : tracks_) journal_.tracks.push_back(t.journal);
    journal_.save(journalPath_);
    writeError_ = false;
    writeErrorText_.clear();
    continueError_.clear();
    continueDone_ = true;
    PF8_LOG_WARN("record", "recording continued in %s", paths::utf8(dir).c_str());
}

bool Recorder::continueElsewhere(const std::filesystem::path& dir, std::string& error)
{
    if (state_.load() == State::Idle)
    {
        error = "not recording";
        return false;
    }
    std::unique_lock lock(mutex_);
    continueRequest_ = dir;
    continueDone_ = false;
    lock.unlock();
    cv_.notify_all();
    for (int i = 0; i < 200; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard l(mutex_);
        if (continueDone_)
        {
            error = continueError_;
            return error.empty();
        }
    }
    error = "timed out";
    return false;
}

void Recorder::workerMain()
{
    writePreroll(); // before any live audio: the files start with the pre-roll
    auto nextCheckpoint = std::chrono::steady_clock::now() + kCheckpoint;
    auto nextRetry = std::chrono::steady_clock::now() + kRetry;
    std::mutex waitMutex;
    while (!stopRequested_.load())
    {
        {
            std::unique_lock w(waitMutex);
            cv_.wait_for(w, std::chrono::milliseconds(50));
        }
        performContinue();
        drainOnce(false);
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextRetry)
        {
            retryErrors();
            nextRetry = now + kRetry;
        }
        if (now >= nextCheckpoint)
        {
            checkpoint();
            nextCheckpoint = now + kCheckpoint;
        }
    }

    // Stop: the tap is inactive; give an in-flight tick time to finish its push, then drain all.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    drainOnce(true);
    retryErrors();
    std::lock_guard lock(mutex_);
    std::vector<CueMarker> cues;
    for (const auto& m : markers_.all()) cues.push_back({m.samplePos, m.label});
    bool ok = true;
    for (auto& t : tracks_)
    {
        const bool main = t.id == static_cast<int>(TrackId::Main);
        const auto e = t.writer->finalise(main ? cues : std::vector<CueMarker>{});
        t.journal.samplesWritten = t.writer->framesWritten();
        t.journal.state = e == SinkError::None ? "finalised" : "failed";
        if (e != SinkError::None)
        {
            ok = false;
            PF8_LOG_ERROR("record", "finalise failed %s: %s (file kept; run recovery)", t.journal.file.c_str(), toString(e));
        }
    }
    journal_.tracks.clear();
    for (const auto& t : tracks_) journal_.tracks.push_back(t.journal);
    journal_.state = ok ? "finalised" : "stopped";
    journal_.updatedUtc = utcNowIso();
    journal_.droppedFrames = tap_.framesDropped();
    journal_.save(journalPath_);
    markers_.save(session_ / "Metadata");
    PF8_LOG_INFO("record", "record.stop session=%s frames=%llu dropped=%llu", paths::utf8(session_).c_str(),
                 static_cast<unsigned long long>(tracks_.empty() ? 0 : tracks_.front().writer->framesWritten()),
                 static_cast<unsigned long long>(tap_.framesDropped()));
    tracks_.clear();
}

Recorder::Status Recorder::status() const
{
    Status s;
    s.state = state_.load();
    s.droppedFrames = tap_.framesDropped();
    s.dropEvents = tap_.dropEvents();
    s.frames = prerollFrames_ + tap_.framesPushed();
    s.seconds = static_cast<double>(s.frames) / rate_;
    s.markerCount = static_cast<int>(markers_.all().size());
    std::lock_guard lock(mutex_);
    s.session = session_;
    s.tracks = static_cast<int>(tracks_.size());
    for (const auto& t : tracks_)
    {
        s.bytesOnDisk += t.writer->bytesOnDisk();
        s.pendingBytes += t.writer->pendingBytes();
    }
    s.writeBytesPerSecond = writeRate_;
    s.disk = disk_;
    s.writeError = writeError_;
    s.writeErrorText = writeErrorText_;
    if (writeError_) s.writeErrorSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - errorSince_).count();
    return s;
}

} // namespace pf8
