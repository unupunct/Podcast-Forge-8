#include "record/FlacWriter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "record/WavWriter.h"

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
    if (failed_ || !impl_->writer) return SinkError::Io;
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
        return impl_->lastError == SinkError::None ? SinkError::Io : impl_->lastError;
    }
    frames_ += static_cast<uint64_t>(frames);
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
