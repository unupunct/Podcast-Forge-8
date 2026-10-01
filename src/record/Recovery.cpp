#include "record/Recovery.h"

#include <cstring>
#include <fstream>

#include "core/Paths.h"
#include "record/FileSink.h"
#include "record/Journal.h"

namespace pf8 {
namespace {

uint32_t rd32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }
void wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }
void wr64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }

void appendLog(const std::filesystem::path& sessionDir, const std::string& line)
{
    std::ofstream log(sessionDir / "Metadata" / "Recovery.log", std::ios::app | std::ios::binary);
    log << utcNowIso() << "  " << line << "\r\n";
}

} // namespace

bool RecoveryReport::ok() const noexcept
{
    for (const auto& t : tracks)
        if (!t.ok) return false;
    return true;
}

RecoveredTrack recoverWavFile(const std::filesystem::path& file)
{
    RecoveredTrack r;
    r.file = file;
    RecoveryFile f;
    if (!f.open(file))
    {
        r.note = "cannot open";
        return r;
    }
    const uint64_t size = f.size();
    uint8_t head[12];
    if (size < 44 || !f.readAt(0, head, 12) || (std::memcmp(head, "RIFF", 4) != 0 && std::memcmp(head, "RF64", 4) != 0) ||
        std::memcmp(head + 8, "WAVE", 4) != 0)
    {
        r.note = "not a WAV file";
        return r;
    }
    // Walk chunks up to 'data'.
    uint64_t pos = 12, junkPos = 0, dataHeader = 0;
    int blockAlign = 0;
    while (pos + 8 <= size)
    {
        uint8_t ch[8];
        if (!f.readAt(pos, ch, 8)) break;
        const uint32_t len = rd32(ch + 4);
        if (std::memcmp(ch, "JUNK", 4) == 0 || std::memcmp(ch, "ds64", 4) == 0) junkPos = pos;
        if (std::memcmp(ch, "fmt ", 4) == 0)
        {
            uint8_t fmt[16];
            if (f.readAt(pos + 8, fmt, 16)) blockAlign = fmt[12] | (fmt[13] << 8);
        }
        if (std::memcmp(ch, "data", 4) == 0)
        {
            dataHeader = pos;
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (dataHeader == 0 || blockAlign <= 0)
    {
        r.note = "no fmt/data chunk";
        return r;
    }
    const uint64_t dataStart = dataHeader + 8;
    const uint64_t available = size > dataStart ? size - dataStart : 0;
    // Only whole frames are declared; a trailing partial frame stays in the file, undeclared.
    uint64_t dataBytes = available - available % static_cast<uint64_t>(blockAlign);
    uint64_t fileEnd = dataStart + dataBytes; // where the RIFF content ends

    // A finalised file has chunks after the audio (cue, LIST/adtl). If the declared data size is
    // followed by well-formed chunks that end exactly at end-of-file, the header is already
    // consistent: keep it, so trailing metadata is never declared as audio.
    {
        uint8_t dh[8];
        uint64_t declared = 0;
        if (f.readAt(dataHeader, dh, 8))
        {
            declared = rd32(dh + 4);
            if (declared == 0xFFFFFFFFu && junkPos != 0)
            {
                uint8_t ds[36];
                if (f.readAt(junkPos, ds, 36) && std::memcmp(ds, "ds64", 4) == 0)
                {
                    declared = 0;
                    for (int i = 7; i >= 0; --i) declared = (declared << 8) | ds[16 + i];
                }
            }
        }
        if (declared > 0 && declared < available && declared % static_cast<uint64_t>(blockAlign) == 0)
        {
            uint64_t p = dataStart + declared + (declared & 1);
            bool wellFormed = p < size;
            while (wellFormed && p < size)
            {
                uint8_t ch[8];
                if (p + 8 > size || !f.readAt(p, ch, 8)) { wellFormed = false; break; }
                const bool known = std::memcmp(ch, "cue ", 4) == 0 || std::memcmp(ch, "LIST", 4) == 0 ||
                                   std::memcmp(ch, "JUNK", 4) == 0 || std::memcmp(ch, "id3 ", 4) == 0;
                const uint32_t len = rd32(ch + 4);
                if (!known || p + 8 + len > size) { wellFormed = false; break; }
                p += 8 + len + (len & 1);
            }
            if (wellFormed && p == size)
            {
                dataBytes = declared;
                fileEnd = size; // the trailing chunks belong to the RIFF
            }
        }
    }
    r.frames = dataBytes / static_cast<uint64_t>(blockAlign);
    // (Any cue chunk after the data is lost in a crash; the markers are in Markers.json.)
    const uint64_t riffSize = fileEnd - 8;

    bool ok;
    if (riffSize < 0xFFFFFFF0ull)
    {
        uint8_t b[4];
        wr32(b, static_cast<uint32_t>(riffSize));
        ok = f.writeAt(0, "RIFF", 4) && f.writeAt(4, b, 4);
        wr32(b, static_cast<uint32_t>(dataBytes));
        ok = ok && f.writeAt(dataHeader + 4, b, 4);
    }
    else if (junkPos != 0)
    {
        uint8_t ds[36];
        std::memcpy(ds, "ds64", 4);
        wr32(ds + 4, 28);
        wr64(ds + 8, riffSize);
        wr64(ds + 16, dataBytes);
        wr64(ds + 24, r.frames);
        wr32(ds + 32, 0);
        uint8_t ff[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        ok = f.writeAt(0, "RF64", 4) && f.writeAt(4, ff, 4) && f.writeAt(junkPos, ds, 36) && f.writeAt(dataHeader + 4, ff, 4);
    }
    else
    {
        r.note = "larger than 4 GB without a reserved ds64 chunk";
        return r;
    }
    r.ok = ok;
    r.note = ok ? "header rebuilt" : "header write failed";
    return r;
}

std::vector<std::filesystem::path> findUnfinishedSessions(const std::filesystem::path& projectDir)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(projectDir, ec))
    {
        if (!e.is_directory()) continue;
        const auto j = Journal::load(e.path() / "Metadata" / "Journal.json");
        if (j && j->state != "finalised" && j->state != "recovered") out.push_back(e.path());
    }
    return out;
}

RecoveryReport recoverSession(const std::filesystem::path& sessionDir)
{
    RecoveryReport rep;
    rep.session = sessionDir;
    const auto jpath = sessionDir / "Metadata" / "Journal.json";
    auto journal = Journal::load(jpath);
    rep.journalFound = journal.has_value();
    std::vector<std::filesystem::path> files;
    if (journal)
        for (const auto& t : journal->tracks) files.push_back(sessionDir / paths::fromUtf8(t.file));
    else
    {
        std::error_code ec;
        for (const char* sub : {"Audio", "Mix"})
            for (const auto& e : std::filesystem::directory_iterator(sessionDir / sub, ec))
                if (e.path().extension() == ".wav") files.push_back(e.path());
    }
    appendLog(sessionDir, "recovery started (" + std::to_string(files.size()) + " tracks)");
    for (size_t i = 0; i < files.size(); ++i)
    {
        RecoveredTrack t;
        const bool flac = journal ? journal->tracks[i].format == FileFormat::Flac : false;
        if (flac)
        {
            // FLAC frames are self-synchronising and the stream length "unknown" is valid: playable as is.
            t.file = files[i];
            std::error_code ec;
            t.ok = std::filesystem::exists(files[i], ec);
            t.note = t.ok ? "FLAC stream kept as is (length marked unknown)" : "missing";
        }
        else
            t = recoverWavFile(files[i]);
        appendLog(sessionDir, paths::utf8(files[i].filename()) + ": " + t.note + ", " + std::to_string(t.frames) + " frames");
        if (journal)
        {
            journal->tracks[i].state = t.ok ? "recovered" : "failed";
            if (t.ok && !flac) journal->tracks[i].samplesWritten = t.frames;
        }
        rep.tracks.push_back(std::move(t));
    }
    if (journal)
    {
        journal->state = rep.ok() ? "recovered" : "stopped";
        journal->updatedUtc = utcNowIso();
        journal->save(jpath);
    }
    appendLog(sessionDir, rep.ok() ? "recovery complete" : "recovery incomplete - original files kept untouched");
    return rep;
}

} // namespace pf8
