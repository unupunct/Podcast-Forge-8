#pragma once
#include <filesystem>

namespace pf8::paths {

std::filesystem::path appData();        // %LOCALAPPDATA%\PodcastForge8
std::filesystem::path logs();           // ...\Logs
std::filesystem::path diagnostics();    // ...\Diagnostics
std::filesystem::path verifyOutput();   // ...\verify
std::filesystem::path settingsDb();     // ...\Settings.db
std::filesystem::path defaultProjects();// Documents\PodcastForge8\Projects

} // namespace pf8::paths
