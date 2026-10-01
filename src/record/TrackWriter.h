#pragma once
// A track file being written. Implementations: WavWriter (WAV / BWF / RF64), FlacWriter.
//
// write() never loses audio: whatever the sink could not take stays in a pending buffer and is
// retried by retryPending(). Nothing here deletes or truncates a file.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "record/FileSink.h"
#include "record/SampleFormat.h"

namespace pf8 {

struct TrackFileInfo
{
    std::filesystem::path path;
    FileFormat format = FileFormat::Wav;
    BitDepth depth = BitDepth::Int24;
    int channels = 1;
    int sampleRate = 48000;
    std::string trackName;
    std::string description;   // BWF bext
    std::string originationDate; // "YYYY-MM-DD"
    std::string originationTime; // "HH:MM:SS"
    uint64_t timeReference = 0;  // samples since midnight at the first sample
};

struct CueMarker
{
    uint64_t samplePos = 0;
    std::string label;
};

class TrackWriter
{
public:
    virtual ~TrackWriter() = default;
    virtual SinkError open(const TrackFileInfo& info, std::unique_ptr<IFileSink> sink) = 0;
    // Interleaved float frames.
    virtual SinkError write(const float* interleaved, int frames) = 0;
    virtual SinkError retryPending() = 0;
    virtual SinkError updateHeader() = 0; // make the file valid for what has been written
    virtual SinkError finalise(const std::vector<CueMarker>& cues) = 0;
    // Hands the not-yet-written audio to another writer (continue on another drive).
    virtual std::vector<uint8_t> takePending() = 0;
    virtual SinkError appendRaw(const std::vector<uint8_t>& bytes) = 0;

    virtual uint64_t framesWritten() const = 0; // frames on disk
    virtual uint64_t pendingBytes() const = 0;
    virtual uint64_t bytesOnDisk() const = 0;
    virtual uint64_t headerBytes() const = 0;
    virtual int blockAlign() const = 0;
    virtual const TrackFileInfo& info() const = 0;
};

std::unique_ptr<TrackWriter> makeTrackWriter(FileFormat f);

} // namespace pf8
