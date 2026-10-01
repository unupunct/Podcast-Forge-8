#pragma once
// Session folders and file naming (RECORDING.md §4).
#include <filesystem>
#include <optional>
#include <string>

namespace pf8 {

// Creates <projectDir>/Session_YYYY-MM-DD_HHMMSS (or _2, _3, … on collision) with Audio/, Mix/,
// Metadata/. Uses CreateDirectoryW so an existing folder is never reused.
std::optional<std::filesystem::path> createSessionFolder(const std::filesystem::path& projectDir, std::string* error = nullptr);

// Filesystem-safe name: strips <>:"/\|?* and control chars, trailing dots/spaces, reserved device
// names (CON, PRN, AUX, NUL, COM1…, LPT1…); empty → "Track".
std::string sanitizeFileName(const std::string& name);

// "<dir>/<stem><ext>", or "<stem>_2<ext>" … when a file of that name already exists.
std::filesystem::path uniqueFilePath(const std::filesystem::path& dir, const std::string& stem, const std::string& ext);

// Local date/time strings for BWF: {"YYYY-MM-DD", "HH:MM:SS", samples since local midnight}.
struct LocalStamp { std::string date, time, folderTag; uint64_t samplesSinceMidnight; };
LocalStamp localStamp(int sampleRate);

} // namespace pf8
