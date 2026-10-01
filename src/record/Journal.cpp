#include "record/Journal.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/Json.h"
#include "record/FileSink.h"

namespace pf8 {

std::string utcNowIso()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond, st.wMilliseconds);
    return buf;
}

namespace {
const char* formatKey(FileFormat f) { return f == FileFormat::Flac ? "flac" : f == FileFormat::Bwf ? "bwf" : "wav"; }
FileFormat formatFrom(const std::string& s) { return s == "flac" ? FileFormat::Flac : s == "bwf" ? FileFormat::Bwf : FileFormat::Wav; }
} // namespace

std::string Journal::toJson() const
{
    json::Object o;
    o["version"] = version;
    o["state"] = state;
    o["sampleRate"] = sampleRate;
    o["startedUtc"] = startedUtc;
    o["updatedUtc"] = updatedUtc;
    o["prerollSamples"] = static_cast<uint64_t>(prerollSamples);
    o["droppedFrames"] = static_cast<uint64_t>(droppedFrames);
    json::Array ts;
    for (const auto& t : tracks)
    {
        json::Object j;
        j["file"] = t.file;
        j["name"] = t.name;
        j["format"] = formatKey(t.format);
        j["bits"] = t.bits;
        j["channels"] = t.channels;
        j["headerBytes"] = static_cast<uint64_t>(t.headerBytes);
        j["blockAlign"] = t.blockAlign;
        j["samplesWritten"] = static_cast<uint64_t>(t.samplesWritten);
        j["state"] = t.state;
        ts.emplace_back(std::move(j));
    }
    o["tracks"] = std::move(ts);
    return json::serialize(json::Value(std::move(o)), true);
}

std::optional<Journal> Journal::fromJson(const std::string& text)
{
    auto v = json::parse(text);
    if (!v || !v->isObject() || !(*v)["tracks"].isArray()) return std::nullopt;
    Journal j;
    j.version = (*v)["version"].asInt(1);
    j.state = (*v)["state"].asString("recording");
    j.sampleRate = (*v)["sampleRate"].asInt(48000);
    j.startedUtc = (*v)["startedUtc"].asString();
    j.updatedUtc = (*v)["updatedUtc"].asString();
    j.prerollSamples = static_cast<uint64_t>((*v)["prerollSamples"].asInt64());
    j.droppedFrames = static_cast<uint64_t>((*v)["droppedFrames"].asInt64());
    for (const auto& t : (*v)["tracks"].asArray())
    {
        JournalTrack jt;
        jt.file = t["file"].asString();
        jt.name = t["name"].asString();
        jt.format = formatFrom(t["format"].asString());
        jt.bits = t["bits"].asInt(24);
        jt.channels = t["channels"].asInt(1);
        jt.headerBytes = static_cast<uint64_t>(t["headerBytes"].asInt64());
        jt.blockAlign = t["blockAlign"].asInt(3);
        jt.samplesWritten = static_cast<uint64_t>(t["samplesWritten"].asInt64());
        jt.state = t["state"].asString("recording");
        j.tracks.push_back(std::move(jt));
    }
    return j;
}

bool Journal::save(const std::filesystem::path& file) const { return writeFileAtomically(file, toJson()); }

std::optional<Journal> Journal::load(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return fromJson(ss.str());
}

} // namespace pf8
