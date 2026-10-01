#pragma once
// Crash recovery (RECORDING.md §5). Rebuilds WAV/BWF/RF64 headers from the real file length.
// It only ever rewrites size fields in the header: no audio byte is changed, removed or deleted.
#include <filesystem>
#include <string>
#include <vector>

namespace pf8 {

struct RecoveredTrack
{
    std::filesystem::path file;
    bool ok = false;
    uint64_t frames = 0;
    std::string note;
};

struct RecoveryReport
{
    std::filesystem::path session;
    bool journalFound = false;
    std::vector<RecoveredTrack> tracks;
    bool ok() const noexcept;
};

// Session folders under `projectDir` whose journal isn't finalised/recovered.
std::vector<std::filesystem::path> findUnfinishedSessions(const std::filesystem::path& projectDir);

// Repairs every track of one session (journal-driven; falls back to scanning Audio/ and Mix/ for WAV files).
RecoveryReport recoverSession(const std::filesystem::path& sessionDir);

// Repairs one WAV/BWF/RF64 file in place (header only). Returns frames now declared.
RecoveredTrack recoverWavFile(const std::filesystem::path& file);

} // namespace pf8
