#pragma once
// A project is a folder (brief §24, ARCHITECTURE.md §8):
//   <Project>\Project.json            settings + carts + playlist + session index
//   <Project>\Session_YYYY-MM-DD_HHMMSS\...  recordings (never touched by project operations)
// Saving is atomic (write a temp file, then replace), so a crash mid-save keeps the previous file.
// Nothing in this module deletes audio.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Json.h"

namespace pf8::project {

constexpr const char* kProjectFile = "Project.json";
constexpr const char* kFormatId = "PodcastForge8.Project";

struct SessionInfo
{
    std::filesystem::path dir;
    std::string startedUtc, state; // from Journal.json ("finalised", "recording" = unfinished …)
    uint64_t frames = 0;
    int sampleRate = 0;
    int markers = 0;
};

struct LoadedProject
{
    std::filesystem::path dir;
    std::string name;
    std::string createdUtc, savedUtc;
    json::Value state;
};

// Creates `<parent>\<name>` (a unique folder name; never reuses an existing project folder) with an
// initial Project.json holding `state`. Returns the folder.
std::optional<std::filesystem::path> createProject(const std::filesystem::path& parent, const std::string& name, const json::Value& state,
                                                   std::string& error);

bool saveProject(const std::filesystem::path& dir, const std::string& name, const json::Value& state, std::string& error);
std::optional<LoadedProject> loadProject(const std::filesystem::path& dir, std::string& error);
bool isProjectDir(const std::filesystem::path& dir);

// "Save As": a new project folder with this state. Recordings stay where they are (the old
// project keeps them); the new project starts without sessions.
std::optional<std::filesystem::path> saveProjectAs(const std::filesystem::path& parent, const std::string& name, const json::Value& state,
                                                   std::string& error);

std::vector<SessionInfo> listSessions(const std::filesystem::path& dir);

// Zips the whole project folder (Project.json, every session's audio and metadata) into `zipFile`
// (must not exist). Audio is stored, not deflated (it barely compresses and it is large).
bool archiveProject(const std::filesystem::path& dir, const std::filesystem::path& zipFile, std::string& error);

// Folder-name-safe version of a project name ("" → "Untitled Project").
std::string sanitizeProjectName(const std::string& name);

} // namespace pf8::project
