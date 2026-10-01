#include "media/MusicPlayer.h"
#include "core/Paths.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "core/Log.h"
#include "dsp/VarResampler.h"

namespace pf8 {

MusicPlayer::MusicPlayer(int sampleRate) : rate_(sampleRate), ring_(static_cast<size_t>(sampleRate) * 2 * 4) // 4 s stereo
{
    decoder_ = std::thread([this] { decoderMain(); });
}

MusicPlayer::~MusicPlayer()
{
    quit_ = true;
    if (decoder_.joinable()) decoder_.join();
}

void MusicPlayer::setPlaylist(std::vector<std::filesystem::path> files)
{
    std::lock_guard lock(mutex_);
    playlist_.clear();
    for (auto& f : files) playlist_.push_back({f, paths::utf8(f.stem()), 0.0, {}});
}

void MusicPlayer::add(const std::filesystem::path& file)
{
    std::lock_guard lock(mutex_);
    playlist_.push_back({file, paths::utf8(file.stem()), 0.0, {}});
}

void MusicPlayer::remove(int index)
{
    std::lock_guard lock(mutex_);
    if (index >= 0 && index < static_cast<int>(playlist_.size())) playlist_.erase(playlist_.begin() + index);
}

std::vector<MusicTrackInfo> MusicPlayer::playlist() const
{
    std::lock_guard lock(mutex_);
    return playlist_;
}

void MusicPlayer::play(int index)
{
    switching_ = true;
    requestedIndex_ = index;
    paused_ = false;
    stopped_ = false;
    startGen_.fetch_add(1);
}

void MusicPlayer::pause() { paused_ = true; }
void MusicPlayer::resume()
{
    if (!stopped_.load()) paused_ = false;
}
void MusicPlayer::next()
{
    const int i = playingIndex_.load();
    play(i + 1);
}
void MusicPlayer::fadeOut(float ms)
{
    fadeMs_ = std::max(5.0f, ms);
    fadeGen_.fetch_add(1);
}
void MusicPlayer::stop()
{
    stopped_ = true;
    paused_ = true;
}

MusicPlayer::Status MusicPlayer::status() const
{
    Status s;
    s.index = playingIndex_.load();
    s.paused = paused_.load();
    s.playing = !stopped_.load() && !s.paused && audible_.load();
    s.position = static_cast<double>(framesPlayed_.load()) / rate_;
    s.duration = duration_.load();
    s.underruns = underruns_.load();
    return s;
}

void MusicPlayer::decoderMain()
{
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader;
    VarResampler rs;
    juce::AudioBuffer<float> in(2, 4096);
    std::vector<float> inter(4096 * 2), out(8192 * 2);
    int maxOut = 8192;
    uint32_t seenStart = 0;
    int64_t readPos = 0;
    bool resampling = false;

    while (!quit_.load())
    {
        const uint32_t g = startGen_.load();
        if (g != seenStart)
        {
            seenStart = g;
            reader.reset();
            // Ask the audio thread to drop whatever is still queued from the previous track.
            const uint32_t req = flushRequest_.fetch_add(1) + 1;
            for (int i = 0; i < 200 && flushAck_.load() != req && !quit_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            framesPlayed_ = 0;
            endOfTrack_ = false;
            const int index = requestedIndex_.load();
            MusicTrackInfo info;
            {
                std::lock_guard lock(mutex_);
                if (index >= 0 && index < static_cast<int>(playlist_.size())) info = playlist_[static_cast<size_t>(index)];
            }
            if (info.path.empty())
            {
                stopped_ = true;
                playingIndex_ = -1;
                continue;
            }
            reader.reset(mgr.createReaderFor(juce::File(juce::String(info.path.wstring().c_str()))));
            std::lock_guard lock(mutex_);
            // The playlist may have changed while the file was opening (no lock held then).
            if (index >= static_cast<int>(playlist_.size()) || playlist_[static_cast<size_t>(index)].path != info.path)
            {
                reader.reset();
                stopped_ = true;
                playingIndex_ = -1;
                continue;
            }
            auto& entry = playlist_[static_cast<size_t>(index)];
            if (!reader)
            {
                entry.error = "unreadable";
                PF8_LOG_WARN("media", "music: cannot open %s", paths::utf8(info.path).c_str());
                stopped_ = true;
                continue;
            }
            entry.seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            duration_ = entry.seconds;
            playingIndex_ = index;
            readPos = 0;
            resampling = std::abs(reader->sampleRate - rate_) > 0.5;
            // Output frames one 4096-frame input chunk can become (low-rate files expand a lot).
            maxOut = static_cast<int>(std::ceil(4096.0 * rate_ / reader->sampleRate)) + VarResampler::kTaps + 16;
            out.assign(static_cast<size_t>(maxOut) * 2, 0.0f);
            if (resampling) rs.prepare(2, reader->sampleRate / rate_, maxOut);
        }

        if (!reader || stopped_.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        // Keep the ring about full; decode in chunks.
        if (ring_.freeSpace() < static_cast<size_t>(maxOut) * 2 + 64)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const int64_t left = reader->lengthInSamples - readPos;
        if (left <= 0)
        {
            if (!endOfTrack_.exchange(true))
            {
                // Auto-advance once the queued audio has played out (the audio thread signals it).
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const int n = static_cast<int>(std::min<int64_t>(4096, left));
        reader->read(&in, 0, n, readPos, true, reader->numChannels > 1);
        if (reader->numChannels == 1) in.copyFrom(1, 0, in, 0, 0, n);
        readPos += n;
        for (int i = 0; i < n; ++i)
        {
            inter[static_cast<size_t>(2 * i)] = in.getSample(0, i);
            inter[static_cast<size_t>(2 * i + 1)] = in.getSample(1, i);
        }
        if (!resampling)
            ring_.push(inter.data(), static_cast<size_t>(n) * 2);
        else
        {
            // Push the whole chunk, draining the resampler as often as needed: nothing is dropped.
            int offset = 0;
            for (int guard = 0; guard < 64 && offset < n; ++guard)
            {
                const int accepted = rs.pushInput(inter.data() + static_cast<size_t>(offset) * 2, n - offset);
                offset += accepted;
                int produced = 0;
                do
                {
                    produced = rs.process(out.data(), maxOut);
                    if (produced > 0) ring_.push(out.data(), static_cast<size_t>(produced) * 2);
                } while (produced == maxOut);
                if (accepted == 0 && produced == 0) break;
            }        }
    }
}

void MusicPlayer::render(float* left, float* right, int frames, const float* duckGain) noexcept
{
    // Flush handshake: drop the previous track's queued audio.
    const uint32_t fr = flushRequest_.load(std::memory_order_acquire);
    if (fr != flushAck_.load(std::memory_order_relaxed))
    {
        ring_.discard(ring_.size());
        gain_ = 0.0f; // the new track fades in
        fade_ = 1.0f;
        fadeStep_ = 0.0f;
        flushAck_.store(fr, std::memory_order_release);
    }
    const uint32_t fg = fadeGen_.load(std::memory_order_acquire);
    if (fg != seenFade_)
    {
        seenFade_ = fg;
        fadeStep_ = fade_ / std::max(1.0f, fadeMs_.load() * 0.001f * static_cast<float>(rate_));
    }
    const bool run = !paused_.load(std::memory_order_relaxed) && !stopped_.load(std::memory_order_relaxed);
    const float target = run ? 1.0f : 0.0f;
    const float rampStep = 1.0f / (0.020f * static_cast<float>(rate_)); // 20 ms pause/resume ramp
    const float vol = std::max(0.0f, volume_.get());
    bool any = false;
    for (int i = 0; i < frames; ++i)
    {
        if (gain_ != target) gain_ = gain_ < target ? std::min(target, gain_ + rampStep) : std::max(target, gain_ - rampStep);
        if (fadeStep_ > 0.0f)
        {
            fade_ = std::max(0.0f, fade_ - fadeStep_);
            if (fade_ <= 0.0f)
            {
                fadeStep_ = 0.0f;
                paused_.store(true, std::memory_order_relaxed);
                fade_ = 1.0f;
                gain_ = 0.0f;
            }
        }
        float lr[2] = {0.0f, 0.0f};
        if (gain_ > 0.0f)
        {
            if (ring_.pop(lr, 2) == 2)
            {
                framesPlayed_.fetch_add(1, std::memory_order_relaxed);
                switching_.store(false, std::memory_order_relaxed);
                any = true;
            }
            else if (!endOfTrack_.load(std::memory_order_relaxed) && run && !switching_.load(std::memory_order_relaxed))
                underruns_.fetch_add(1, std::memory_order_relaxed);
        }
        const float g = gain_ * fade_ * vol * (duckGain ? duckGain[i] : 1.0f);
        left[i] = lr[0] * g;
        right[i] = lr[1] * g;
    }
    audible_.store(any || gain_ > 0.0f, std::memory_order_relaxed);
    // End of track: the decoder reached the end and the ring is empty.
    if (run && endOfTrack_.load(std::memory_order_relaxed) && ring_.size() == 0 && playingIndex_.load() >= 0)
    {
        endOfTrack_.store(false, std::memory_order_relaxed);
        if (autoAdvance_.load(std::memory_order_relaxed))
        {
            switching_.store(true, std::memory_order_relaxed);
            requestedIndex_.store(playingIndex_.load() + 1, std::memory_order_relaxed);
            startGen_.fetch_add(1, std::memory_order_release);
        }
        else
            stopped_.store(true, std::memory_order_relaxed);
    }
}

} // namespace pf8
