#pragma once
// Recording markers (RECORDING.md §8): stored immediately in Metadata/Markers.json and .csv,
// exportable to CSV, Audacity labels, Adobe Audition CSV and REAPER region/marker CSV.
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pf8 {

struct Marker
{
    int id = 0;
    uint64_t samplePos = 0; // from the start of the session's files (the pre-roll starts at 0)
    std::string label;
    std::string colour = "#f2b134";
    std::string createdUtc;
};

enum class MarkerExport : uint8_t { Csv, Audacity, Audition, Reaper };
const char* toString(MarkerExport e) noexcept;
const char* extension(MarkerExport e) noexcept;

std::string timecode(uint64_t samples, int sampleRate, bool millis = true); // HH:MM:SS(.mmm)

class MarkerList
{
public:
    void setSampleRate(int r) { rate_ = r; }
    int add(uint64_t samplePos, std::string label = {});
    bool rename(int id, std::string label);
    bool remove(int id); // metadata only
    std::vector<Marker> all() const;
    void clear();

    std::string toJson() const;
    bool loadJson(const std::string& text);
    std::string exportText(MarkerExport format) const;

    // Writes Markers.json + Markers.csv atomically into `metadataDir` (no-op when empty dir).
    bool save(const std::filesystem::path& metadataDir) const;

private:
    mutable std::mutex mutex_;
    std::vector<Marker> markers_;
    int nextId_ = 1;
    int rate_ = 48000;
};

} // namespace pf8
