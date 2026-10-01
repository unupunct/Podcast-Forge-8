#include "record/FlacWriter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "record/WavWriter.h"

#include <algorithm>
#include <cstring>

namespace pf8 {
namespace {

// juce::OutputStream over an IFileSink. Seeking back (for STREAMINFO) patches already-written bytes.
class SinkOutputStream : public juce::OutputStream
{
public:
    explicit SinkOutputStream(IFileSink& sink, SinkError& error) : sink_(sink), error_(error) {}
    void flush() override { sink_.flush(); }
    bool setPosition(juce::int64 p) override
    {
        if (p < 0 || static_cast<uint64_t>(p) > sink_.size()) return false;
        pos_ = static_cast<uint64_t>(p);
        return true;
    }
    juce::int64 getPosition() override { return static_cast<juce::int64>(pos_); }
    bool write(const void* data, size_t n) override
    {
        const auto size = sink_.size();
        SinkError e = SinkError::None;
        if (pos_ < size)
        {
            const size_t overlap = static_cast<size_t>(std::min<uint64_t>(n, size - pos_));
            e = sink_.writeAt(pos_, data, overlap);
            if (e == SinkError::None && overlap < n)
            {
                size_t w = 0;
                e = sink_.append(static_cast<const char*>(data) + overlap, n - overlap, w);
            }
        }
        else
        {
            size_t w = 0;
            e = sink_.append(data, n, w);
        }
        if (e != SinkError::None)
        {
            error_ = e;
            return false;
        }
        pos_ += n;
        return true;
    }

private:
    IFileSink& sink_;
    SinkError& error_;
    uint64_t pos_ = 0;
};

} // namespace

struct FlacWriter::Impl
{
    std::unique_ptr<IFileSink> sink;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::AudioBuffer<float> buffer;
    SinkError lastError = SinkError::None;
};

FlacWriter::FlacWriter() : impl_(std::make_unique<Impl>()) {}

FlacWriter::~FlacWriter()
{
    impl_->writer.reset(); // writes STREAMINFO
    if (impl_->sink) impl_->sink->close();
}

SinkError FlacWriter::open(const TrackFileInfo& info, std::unique_ptr<IFileSink> sink)
{
    info_ = info;
    if (info_.depth == BitDepth::Float32) info_.depth = BitDepth::Int24; // FLAC is integer-only
    impl_->sink = std::move(sink);
    if (const auto e = impl_->sink->create(info_.path); e != SinkError::None) return e;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<SinkOutputStream>(*impl_->sink, impl_->lastError);
    juce::FlacAudioFormat flac;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(info_.sampleRate)
                             .withNumChannels(info_.channels)
                             .withBitsPerSample(static_cast<int>(info_.depth))
                             .withQualityOptionIndex(5);
    impl_->writer = flac.createWriterFor(stream, options);
    return impl_->writer ? SinkError::None : SinkError::Io;
}

SinkError FlacWriter::write(const float* interleaved, int frames)
{
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(info_.channels);
    if (failed_ || !impl_->writer)
    {
        // The stream is broken: keep the audio (never drop it) until it continues elsewhere.
        held_.insert(held_.end(), interleaved, interleaved + samples);
        return SinkError::Io;
    }
    auto& buf = impl_->buffer;
    if (buf.getNumChannels() != info_.channels || buf.getNumSamples() < frames)
        buf.setSize(info_.channels, std::max(frames, 48000), false, false, true);
    for (int c = 0; c < info_.channels; ++c)
    {
        float* d = buf.getWritePointer(c);
        for (int i = 0; i < frames; ++i) d[i] = interleaved[static_cast<size_t>(i) * static_cast<size_t>(info_.channels) + static_cast<size_t>(c)];
    }
    if (!impl_->writer->writeFromAudioSampleBuffer(buf, 0, frames))
    {
        failed_ = true;
        held_.insert(held_.end(), interleaved, interleaved + samples); // this block too
        return impl_->lastError == SinkError::None ? SinkError::Io : impl_->lastError;
    }
    frames_ += static_cast<uint64_t>(frames);
    return SinkError::None;
}

std::vector<uint8_t> FlacWriter::takePending()
{
    std::vector<uint8_t> bytes(held_.size() * sizeof(float));
    if (!bytes.empty()) std::memcpy(bytes.data(), held_.data(), bytes.size());
    held_.clear();
    held_.shrink_to_fit();
    return bytes;
}

SinkError FlacWriter::appendRaw(const std::vector<uint8_t>& bytes)
{
    const size_t frameBytes = sizeof(float) * static_cast<size_t>(info_.channels);
    if (bytes.size() < frameBytes) return SinkError::None;
    std::vector<float> f(bytes.size() / sizeof(float));
    std::memcpy(f.data(), bytes.data(), f.size() * sizeof(float));
    // Encode in blocks (the write buffer is sized per call).
    const int total = static_cast<int>(bytes.size() / frameBytes);
    for (int done = 0; done < total;)
    {
        const int n = std::min(48000, total - done);
        const auto e = write(f.data() + static_cast<size_t>(done) * static_cast<size_t>(info_.channels), n);
        if (e != SinkError::None)
        {
            // Not encoded: write() kept these frames; keep the rest as well.
            const size_t from = static_cast<size_t>(done + n) * static_cast<size_t>(info_.channels);
            held_.insert(held_.end(), f.begin() + static_cast<std::ptrdiff_t>(from), f.end());
            return e;
        }
        done += n;
    }
    return SinkError::None;
}

SinkError FlacWriter::updateHeader()
{
    if (impl_->writer) impl_->writer->flush();
    return impl_->sink ? impl_->sink->flush() : SinkError::Io;
}

SinkError FlacWriter::finalise(const std::vector<CueMarker>&)
{
    impl_->writer.reset();
    if (!impl_->sink) return SinkError::Io;
    impl_->sink->flush();
    impl_->sink->close();
    return failed_ ? SinkError::Io : SinkError::None;
}

uint64_t FlacWriter::bytesOnDisk() const { return impl_->sink ? impl_->sink->size() : 0; }

std::unique_ptr<TrackWriter> makeTrackWriter(FileFormat f)
{
    switch (f)
    {
        case FileFormat::Wav: return std::make_unique<WavWriter>(false);
        case FileFormat::Bwf: return std::make_unique<WavWriter>(true);
        case FileFormat::Flac: return std::make_unique<FlacWriter>();
    }
    return std::make_unique<WavWriter>(false);
}

} // namespace pf8
