#pragma once
// One event-driven WASAPI stream (capture or render) opened by endpoint ID.
//
// open() and start()/stop() run on a control thread. The stream owns an MMCSS "Pro Audio" thread
// that converts between the device format and interleaved float and calls the StreamCallback.
#include <atomic>
#include <memory>
#include <string>

#include "engine/StreamTypes.h"

namespace pf8 {

class WasapiStream
{
public:
    WasapiStream(StreamConfig config, StreamCallback* callback);
    ~WasapiStream();
    WasapiStream(const WasapiStream&) = delete;
    WasapiStream& operator=(const WasapiStream&) = delete;

    bool open(std::string& error);
    bool start();
    void stop();

    const StreamConfig& config() const noexcept { return config_; }
    StreamStatus status() const noexcept { return status_.load(); }
    int sampleRate() const noexcept { return sampleRate_; }
    int channels() const noexcept { return channels_; }
    int periodFrames() const noexcept { return periodFrames_; }
    int bufferFrames() const noexcept { return bufferFrames_; }
    StreamMode grantedMode() const noexcept { return grantedMode_; }
    const char* sampleFormatName() const noexcept;
    const StreamStats& stats() const noexcept { return stats_; }

private:
    struct Impl;
    void threadMain();

    StreamConfig config_;
    StreamCallback* callback_;
    std::unique_ptr<Impl> impl_;
    std::atomic<StreamStatus> status_{StreamStatus::Closed};
    std::atomic<bool> stopRequested_{false};
    StreamStats stats_;
    int sampleRate_ = 0;
    int channels_ = 0;
    int periodFrames_ = 0;
    int bufferFrames_ = 0;
    StreamMode grantedMode_ = StreamMode::Shared;
};

} // namespace pf8
