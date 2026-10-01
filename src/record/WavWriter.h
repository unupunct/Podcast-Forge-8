#pragma once
// WAV / Broadcast WAV writer with RF64 promotion (EBU TECH 3306).
//
// Layout: RIFF | JUNK(28, reserved for ds64) | fmt | [bext] | [iXML] | data … | [cue + LIST adtl]
// The header is patched every few seconds so a crash leaves a valid file up to the last patch, and
// Recovery can always rebuild the sizes from the file length. Beyond `rf64Threshold` bytes the JUNK
// chunk becomes ds64 and the 32-bit sizes are set to 0xFFFFFFFF.
#include "record/TrackWriter.h"

namespace pf8 {

class WavWriter : public TrackWriter
{
public:
    static constexpr uint64_t kDefaultRf64Threshold = 0xFFFFFFFFull - (64ull << 20); // 4 GB minus headroom

    explicit WavWriter(bool bwf, uint64_t rf64Threshold = kDefaultRf64Threshold) : bwf_(bwf), rf64Threshold_(rf64Threshold) {}
    ~WavWriter() override;

    SinkError open(const TrackFileInfo& info, std::unique_ptr<IFileSink> sink) override;
    SinkError write(const float* interleaved, int frames) override;
    SinkError retryPending() override;
    SinkError updateHeader() override;
    SinkError finalise(const std::vector<CueMarker>& cues) override;
    std::vector<uint8_t> takePending() override;
    SinkError appendRaw(const std::vector<uint8_t>& bytes) override;

    uint64_t framesWritten() const override { return dataBytes_ / static_cast<uint64_t>(blockAlign_); }
    uint64_t pendingBytes() const override { return pending_.size(); }
    uint64_t bytesOnDisk() const override { return sink_ ? sink_->size() : 0; }
    uint64_t headerBytes() const override { return headerBytes_; }
    int blockAlign() const override { return blockAlign_; }
    const TrackFileInfo& info() const override { return info_; }
    bool isRf64() const noexcept { return rf64_; }

    // Chunk offsets (shared with Recovery).
    static constexpr uint64_t kJunkOffset = 12;

private:
    SinkError flushPending();

    bool bwf_;
    uint64_t rf64Threshold_;
    TrackFileInfo info_;
    std::unique_ptr<IFileSink> sink_;
    Ditherer dither_;
    std::vector<uint8_t> staging_, pending_;
    std::vector<uint8_t> tornPrefix_; // bytes of an incomplete frame that reached the disk (disk full mid-frame)
    int blockAlign_ = 3;
    uint64_t headerBytes_ = 0, dataSizeOffset_ = 0, dataBytes_ = 0;
    bool rf64_ = false, finalised_ = false;
};

} // namespace pf8
