#pragma once
// FLAC track writer (JUCE FlacAudioFormat) streaming into an IFileSink, so FLAC files get the same
// CREATE_NEW protection as WAV. Until finalise() the STREAMINFO total-sample count is 0 ("unknown"),
// which decoders accept — an interrupted FLAC file stays playable up to its last complete frame.
//
// If the disk refuses a write, the encoder's stream cannot be resumed: from then on the audio is
// held in memory (as float frames, counted in pendingBytes()) until the recording continues in a
// new `_part2` file elsewhere (takePending / appendRaw re-encode it there) or is rescued at stop.
// The frames of the failed block may also be partly in the old file (never lost, at most doubled).
#include "record/TrackWriter.h"

namespace juce {
class AudioFormatWriter;
template <typename T> class AudioBuffer;
} // namespace juce

namespace pf8 {

class FlacWriter : public TrackWriter
{
public:
    FlacWriter();
    ~FlacWriter() override;

    SinkError open(const TrackFileInfo& info, std::unique_ptr<IFileSink> sink) override;
    SinkError write(const float* interleaved, int frames) override;
    SinkError retryPending() override { return failed_ ? SinkError::Io : SinkError::None; }
    SinkError updateHeader() override;
    SinkError finalise(const std::vector<CueMarker>& cues) override;
    std::vector<uint8_t> takePending() override;                       // the held float frames, as bytes
    SinkError appendRaw(const std::vector<uint8_t>& bytes) override;   // float frames from another FlacWriter

    uint64_t framesWritten() const override { return frames_; }
    uint64_t pendingBytes() const override { return held_.size() * sizeof(float); }
    uint64_t bytesOnDisk() const override;
    uint64_t headerBytes() const override { return 0; }
    int blockAlign() const override { return bytesPerSample(info_.depth) * info_.channels; }
    const TrackFileInfo& info() const override { return info_; }

    struct Impl;

private:
    TrackFileInfo info_;
    std::unique_ptr<Impl> impl_;
    uint64_t frames_ = 0;
    bool failed_ = false;
    std::vector<float> held_; // interleaved frames not encoded since the stream failed
};

} // namespace pf8
