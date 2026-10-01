#include "record/WavWriter.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace pf8 {
namespace {

void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void put64(std::vector<uint8_t>& v, uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i))); }
void putTag(std::vector<uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }
void putFixed(std::vector<uint8_t>& v, const std::string& s, size_t n)
{
    for (size_t i = 0; i < n; ++i) v.push_back(i < s.size() ? static_cast<uint8_t>(s[i]) : 0);
}
std::array<uint8_t, 4> le32(uint32_t x) { return {static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x >> 16), static_cast<uint8_t>(x >> 24)}; }

std::string xmlEscape(const std::string& s)
{
    std::string o;
    for (char c : s)
        switch (c)
        {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    return o;
}

constexpr size_t kFlushThreshold = 256 * 1024;
constexpr size_t kMaxPending = 256ull << 20; // audio kept in memory while the disk refuses writes

} // namespace

WavWriter::~WavWriter()
{
    // Never leave a file without a valid header; never delete it.
    if (sink_ && !finalised_)
    {
        flushPending();
        updateHeader();
        sink_->close();
    }
}

SinkError WavWriter::open(const TrackFileInfo& info, std::unique_ptr<IFileSink> sink)
{
    info_ = info;
    sink_ = std::move(sink);
    if (const auto e = sink_->create(info.path); e != SinkError::None) return e;

    const int bps = bytesPerSample(info.depth);
    blockAlign_ = bps * info.channels;
    std::vector<uint8_t> h;
    putTag(h, "RIFF");
    put32(h, 0); // patched
    putTag(h, "WAVE");
    // JUNK reserved for ds64 (RF64): size 28 = riffSize64, dataSize64, sampleCount64, tableLength.
    putTag(h, "JUNK");
    put32(h, 28);
    h.insert(h.end(), 28, 0);
    // fmt
    const bool isFloat = info.depth == BitDepth::Float32;
    putTag(h, "fmt ");
    put32(h, 16);
    put16(h, isFloat ? 3 : 1);
    put16(h, static_cast<uint16_t>(info.channels));
    put32(h, static_cast<uint32_t>(info.sampleRate));
    put32(h, static_cast<uint32_t>(info.sampleRate * blockAlign_));
    put16(h, static_cast<uint16_t>(blockAlign_));
    put16(h, static_cast<uint16_t>(bps * 8));
    if (bwf_)
    {
        // bext (EBU TECH 3285 v1): 602 bytes + coding history.
        const std::string history = "A=PCM,F=" + std::to_string(info.sampleRate) + ",W=" + std::to_string(bps * 8) + ",M=" +
                                    (info.channels == 1 ? "mono" : "stereo") + ",T=Podcast Forge 8\r\n";
        std::vector<uint8_t> b;
        putFixed(b, info.description, 256);
        putFixed(b, "Podcast Forge 8", 32);
        putFixed(b, info.trackName, 32);
        putFixed(b, info.originationDate, 10);
        putFixed(b, info.originationTime, 8);
        put64(b, info.timeReference);
        put16(b, 1);          // version
        b.insert(b.end(), 64, 0); // UMID
        b.insert(b.end(), 10, 0); // loudness fields (v2), zero = not given
        b.insert(b.end(), 180, 0); // reserved
        b.insert(b.end(), history.begin(), history.end());
        if (b.size() & 1) b.push_back(0);
        putTag(h, "bext");
        put32(h, static_cast<uint32_t>(b.size()));
        h.insert(h.end(), b.begin(), b.end());

        std::string ixml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><BWFXML><IXML_VERSION>2.10</IXML_VERSION>"
                           "<PROJECT>Podcast Forge 8</PROJECT><TRACK_LIST><TRACK_COUNT>1</TRACK_COUNT><TRACK>"
                           "<CHANNEL_INDEX>1</CHANNEL_INDEX><INTERLEAVE_INDEX>1</INTERLEAVE_INDEX><NAME>" +
                           xmlEscape(info.trackName) + "</NAME></TRACK></TRACK_LIST></BWFXML>";
        if (ixml.size() & 1) ixml.push_back(' ');
        putTag(h, "iXML");
        put32(h, static_cast<uint32_t>(ixml.size()));
        h.insert(h.end(), ixml.begin(), ixml.end());
    }
    putTag(h, "data");
    dataSizeOffset_ = h.size();
    put32(h, 0);
    headerBytes_ = h.size();
    size_t w = 0;
    if (const auto e = sink_->append(h.data(), h.size(), w); e != SinkError::None) return e;
    staging_.reserve(kFlushThreshold * 2);
    return updateHeader();
}

SinkError WavWriter::write(const float* interleaved, int frames)
{
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(info_.channels);
    const size_t bytes = samples * static_cast<size_t>(bytesPerSample(info_.depth));
    const size_t old = pending_.size();
    if (old + bytes > kMaxPending) return SinkError::DiskFull; // caller keeps the audio in its ring
    pending_.resize(old + bytes);
    dither_.convert(interleaved, samples, info_.depth, pending_.data() + old);
    if (pending_.size() >= kFlushThreshold) return flushPending();
    return SinkError::None;
}

SinkError WavWriter::flushPending()
{
    if (pending_.empty() || !sink_) return SinkError::None;
    // Only whole frames go to disk so the data size is always frame-aligned. After a torn write the
    // buffer starts mid-frame, so alignment is measured on the file position, not the buffer start.
    const size_t over = static_cast<size_t>((dataBytes_ + pending_.size()) % static_cast<uint64_t>(blockAlign_));
    const size_t whole = pending_.size() >= over ? pending_.size() - over : 0;
    size_t written = 0;
    const auto e = sink_->append(pending_.data(), whole, written);
    // Remember the start of a frame torn by the disk filling up: the header excludes it, and if
    // recording continues in another file that file must start with the whole frame.
    dataBytes_ += written;
    const size_t tail = static_cast<size_t>(dataBytes_ % static_cast<uint64_t>(blockAlign_));
    if (tail == 0) tornPrefix_.clear();
    else
    {
        if (written >= tail) tornPrefix_.assign(pending_.begin() + static_cast<ptrdiff_t>(written - tail), pending_.begin() + static_cast<ptrdiff_t>(written));
        else tornPrefix_.insert(tornPrefix_.end(), pending_.begin(), pending_.begin() + static_cast<ptrdiff_t>(written)); // frame still incomplete
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<ptrdiff_t>(written));
    return e;
}

SinkError WavWriter::retryPending() { return flushPending(); }

SinkError WavWriter::updateHeader()
{
    if (!sink_) return SinkError::Io;
    const uint64_t alignedData = dataBytes_ - dataBytes_ % static_cast<uint64_t>(blockAlign_);
    const uint64_t riffSize = sink_->size() - 8;
    if (!rf64_ && sink_->size() > rf64Threshold_) rf64_ = true;
    if (!rf64_)
    {
        const auto r = le32(static_cast<uint32_t>(riffSize));
        const auto d = le32(static_cast<uint32_t>(alignedData));
        if (auto e = sink_->writeAt(4, r.data(), 4); e != SinkError::None) return e;
        if (auto e = sink_->writeAt(dataSizeOffset_, d.data(), 4); e != SinkError::None) return e;
        return sink_->flush();
    }
    // RF64: "RF64", sizes in ds64.
    std::vector<uint8_t> ds;
    putTag(ds, "ds64");
    put32(ds, 28);
    put64(ds, riffSize);
    put64(ds, alignedData);
    put64(ds, alignedData / static_cast<uint64_t>(blockAlign_));
    put32(ds, 0);
    const uint8_t tag[4] = {'R', 'F', '6', '4'};
    const auto ff = le32(0xFFFFFFFFu);
    if (auto e = sink_->writeAt(0, tag, 4); e != SinkError::None) return e;
    if (auto e = sink_->writeAt(4, ff.data(), 4); e != SinkError::None) return e;
    if (auto e = sink_->writeAt(kJunkOffset, ds.data(), ds.size()); e != SinkError::None) return e;
    if (auto e = sink_->writeAt(dataSizeOffset_, ff.data(), 4); e != SinkError::None) return e;
    return sink_->flush();
}

SinkError WavWriter::finalise(const std::vector<CueMarker>& cues)
{
    if (!sink_) return SinkError::Io;
    auto e = flushPending();
    if (e == SinkError::None && !pending_.empty()) e = SinkError::Io;
    if (e == SinkError::None && !cues.empty() && dataBytes_ % static_cast<uint64_t>(blockAlign_) == 0)
    {
        std::vector<uint8_t> c;
        if (dataBytes_ & 1) c.push_back(0); // chunks are word-aligned
        putTag(c, "cue ");
        put32(c, static_cast<uint32_t>(4 + 24 * cues.size()));
        put32(c, static_cast<uint32_t>(cues.size()));
        for (size_t i = 0; i < cues.size(); ++i)
        {
            put32(c, static_cast<uint32_t>(i + 1));
            put32(c, static_cast<uint32_t>(cues[i].samplePos));
            putTag(c, "data");
            put32(c, 0);
            put32(c, 0);
            put32(c, static_cast<uint32_t>(cues[i].samplePos));
        }
        std::vector<uint8_t> adtl;
        putTag(adtl, "adtl");
        for (size_t i = 0; i < cues.size(); ++i)
        {
            std::string text = cues[i].label;
            text.push_back('\0');
            if (text.size() & 1) text.push_back('\0');
            putTag(adtl, "labl");
            put32(adtl, static_cast<uint32_t>(4 + text.size()));
            put32(adtl, static_cast<uint32_t>(i + 1));
            adtl.insert(adtl.end(), text.begin(), text.end());
        }
        putTag(c, "LIST");
        put32(c, static_cast<uint32_t>(adtl.size()));
        c.insert(c.end(), adtl.begin(), adtl.end());
        size_t w = 0;
        e = sink_->append(c.data(), c.size(), w);
    }
    const auto h = updateHeader();
    if (e == SinkError::None) e = h;
    sink_->flush();
    sink_->close();
    finalised_ = true;
    return e;
}

std::vector<uint8_t> WavWriter::takePending()
{
    // Whole frames only: the torn frame's prefix (excluded from this file's header) goes first.
    std::vector<uint8_t> p = tornPrefix_;
    p.insert(p.end(), pending_.begin(), pending_.end());
    pending_.clear();
    tornPrefix_.clear();
    return p;
}

SinkError WavWriter::appendRaw(const std::vector<uint8_t>& bytes)
{
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    return flushPending();
}

} // namespace pf8
