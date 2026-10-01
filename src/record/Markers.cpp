#include "record/Markers.h"

#include <algorithm>
#include <cstdio>

#include "core/Json.h"
#include "record/FileSink.h"
#include "record/Journal.h"

namespace pf8 {
namespace {

std::string csvQuote(const std::string& s)
{
    std::string o = "\"";
    for (char c : s) o += c == '"' ? std::string("\"\"") : std::string(1, c);
    return o + "\"";
}

std::string seconds(uint64_t samples, int rate)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.6f", static_cast<double>(samples) / rate);
    return b;
}

} // namespace

const char* toString(MarkerExport e) noexcept
{
    switch (e)
    {
        case MarkerExport::Csv: return "CSV";
        case MarkerExport::Audacity: return "Audacity labels";
        case MarkerExport::Audition: return "Adobe Audition markers";
        case MarkerExport::Reaper: return "REAPER markers";
    }
    return "?";
}

const char* extension(MarkerExport e) noexcept
{
    switch (e)
    {
        case MarkerExport::Csv: return ".csv";
        case MarkerExport::Audacity: return ".audacity.txt";
        case MarkerExport::Audition: return ".audition.csv";
        case MarkerExport::Reaper: return ".reaper.csv";
    }
    return ".txt";
}

std::string timecode(uint64_t samples, int rate, bool millis)
{
    const uint64_t totalMs = samples * 1000 / static_cast<uint64_t>(rate);
    const uint64_t h = totalMs / 3600000, m = (totalMs / 60000) % 60, s = (totalMs / 1000) % 60, ms = totalMs % 1000;
    char b[32];
    if (millis) std::snprintf(b, sizeof b, "%02llu:%02llu:%02llu.%03llu", h, m, s, ms);
    else std::snprintf(b, sizeof b, "%02llu:%02llu:%02llu", h, m, s);
    return b;
}

int MarkerList::add(uint64_t samplePos, std::string label)
{
    std::lock_guard lock(mutex_);
    Marker m;
    m.id = nextId_++;
    m.samplePos = samplePos;
    m.label = label.empty() ? "Marker " + std::to_string(m.id) : std::move(label);
    m.createdUtc = utcNowIso();
    markers_.push_back(m);
    std::stable_sort(markers_.begin(), markers_.end(), [](const Marker& a, const Marker& b) { return a.samplePos < b.samplePos; });
    return m.id;
}

bool MarkerList::rename(int id, std::string label)
{
    std::lock_guard lock(mutex_);
    for (auto& m : markers_)
        if (m.id == id)
        {
            m.label = std::move(label);
            return true;
        }
    return false;
}

bool MarkerList::remove(int id)
{
    std::lock_guard lock(mutex_);
    const auto it = std::remove_if(markers_.begin(), markers_.end(), [id](const Marker& m) { return m.id == id; });
    const bool found = it != markers_.end();
    markers_.erase(it, markers_.end());
    return found;
}

std::vector<Marker> MarkerList::all() const
{
    std::lock_guard lock(mutex_);
    return markers_;
}

void MarkerList::clear()
{
    std::lock_guard lock(mutex_);
    markers_.clear();
    nextId_ = 1;
}

std::string MarkerList::toJson() const
{
    json::Object root;
    root["sampleRate"] = rate_;
    json::Array arr;
    for (const auto& m : all())
    {
        json::Object o;
        o["id"] = m.id;
        o["sample"] = static_cast<uint64_t>(m.samplePos);
        o["timecode"] = timecode(m.samplePos, rate_);
        o["label"] = m.label;
        o["colour"] = m.colour;
        o["createdUtc"] = m.createdUtc;
        arr.emplace_back(std::move(o));
    }
    root["markers"] = std::move(arr);
    return json::serialize(json::Value(std::move(root)), true);
}

bool MarkerList::loadJson(const std::string& text)
{
    auto v = json::parse(text);
    if (!v || !(*v)["markers"].isArray()) return false;
    std::lock_guard lock(mutex_);
    markers_.clear();
    nextId_ = 1;
    rate_ = (*v)["sampleRate"].asInt(rate_);
    for (const auto& o : (*v)["markers"].asArray())
    {
        Marker m;
        m.id = o["id"].asInt();
        m.samplePos = static_cast<uint64_t>(o["sample"].asInt64());
        m.label = o["label"].asString();
        m.colour = o["colour"].asString("#f2b134");
        m.createdUtc = o["createdUtc"].asString();
        nextId_ = std::max(nextId_, m.id + 1);
        markers_.push_back(std::move(m));
    }
    return true;
}

std::string MarkerList::exportText(MarkerExport format) const
{
    const auto ms = all();
    std::string out;
    switch (format)
    {
        case MarkerExport::Csv:
            out = "Timecode,Seconds,Label\r\n";
            for (const auto& m : ms) out += timecode(m.samplePos, rate_) + "," + seconds(m.samplePos, rate_) + "," + csvQuote(m.label) + "\r\n";
            break;
        case MarkerExport::Audacity: // start<TAB>end<TAB>label (point labels: start = end)
            for (const auto& m : ms) out += seconds(m.samplePos, rate_) + "\t" + seconds(m.samplePos, rate_) + "\t" + m.label + "\n";
            break;
        case MarkerExport::Audition: // Audition "Marker List" CSV (tab-separated, decimal time)
            out = "Name\tStart\tDuration\tTime Format\tType\tDescription\r\n";
            for (const auto& m : ms)
            {
                const auto tc = timecode(m.samplePos, rate_);
                out += m.label + "\t" + tc.substr(1) + "\t0:00.000\tdecimal\tCue\t\r\n";
            }
            break;
        case MarkerExport::Reaper:
            out = "#,Name,Start,End,Length\r\n";
            for (const auto& m : ms)
                out += "M" + std::to_string(m.id) + "," + csvQuote(m.label) + "," + timecode(m.samplePos, rate_) + ",,\r\n";
            break;
    }
    return out;
}

bool MarkerList::save(const std::filesystem::path& metadataDir) const
{
    if (metadataDir.empty()) return false;
    return writeFileAtomically(metadataDir / "Markers.json", toJson()) &&
           writeFileAtomically(metadataDir / "Markers.csv", exportText(MarkerExport::Csv));
}

} // namespace pf8
