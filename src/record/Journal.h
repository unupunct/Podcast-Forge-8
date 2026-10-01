#pragma once
// Session journal (RECORDING.md §5): Metadata/Journal.json, rewritten atomically every ~2 s.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "record/SampleFormat.h"

namespace pf8 {

struct JournalTrack
{
    std::string file; // relative to the session folder, '/' separators
    std::string name;
    FileFormat format = FileFormat::Wav;
    int bits = 24;
    int channels = 1;
    uint64_t headerBytes = 0;
    int blockAlign = 3;
    uint64_t samplesWritten = 0;
    std::string state = "recording"; // recording | finalised | recovered | failed
};

struct Journal
{
    int version = 1;
    std::string state = "recording"; // recording | paused | stopped | finalised | recovered
    int sampleRate = 48000;
    std::string startedUtc, updatedUtc;
    uint64_t prerollSamples = 0;
    uint64_t droppedFrames = 0;
    std::vector<JournalTrack> tracks;

    std::string toJson() const;
    static std::optional<Journal> fromJson(const std::string& text);
    bool save(const std::filesystem::path& file) const;
    static std::optional<Journal> load(const std::filesystem::path& file);
};

std::string utcNowIso();

} // namespace pf8
