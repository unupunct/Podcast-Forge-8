#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace pf8::paths {

std::filesystem::path appData();        // %LOCALAPPDATA%\PodcastForge8
std::filesystem::path logs();           // ...\Logs
std::filesystem::path diagnostics();    // ...\Diagnostics
std::filesystem::path verifyOutput();   // ...\verify
std::filesystem::path settingsDb();     // ...\Settings.db
std::filesystem::path defaultProjects();// Documents\PodcastForge8\Projects

// UTF-8 conversions. Never use path::string() / generic_string() on user-named paths: they convert
// through the ANSI code page and throw for characters outside it (e.g. Romanian ș ț on an English
// Windows).
inline std::string utf8(const std::filesystem::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
inline std::string utf8Generic(const std::filesystem::path& p)
{
    const auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}
inline std::filesystem::path fromUtf8(std::string_view s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

} // namespace pf8::paths
