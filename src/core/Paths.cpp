#include "core/Paths.h"

#include <windows.h>
#include <shlobj.h>

namespace pf8::paths {
namespace {

std::filesystem::path knownFolder(REFKNOWNFOLDERID id, const wchar_t* fallbackEnv)
{
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw)
        result = raw;
    CoTaskMemFree(raw);
    if (result.empty())
    {
        wchar_t buf[MAX_PATH]{};
        if (GetEnvironmentVariableW(fallbackEnv, buf, MAX_PATH) > 0) result = buf;
    }
    return result;
}

} // namespace

std::filesystem::path appData() { return knownFolder(FOLDERID_LocalAppData, L"LOCALAPPDATA") / L"PodcastForge8"; }
std::filesystem::path logs() { return appData() / L"Logs"; }
std::filesystem::path diagnostics() { return appData() / L"Diagnostics"; }
std::filesystem::path verifyOutput() { return appData() / L"verify"; }
std::filesystem::path settingsDb() { return appData() / L"Settings.db"; }
std::filesystem::path defaultProjects()
{
    return knownFolder(FOLDERID_Documents, L"USERPROFILE") / L"PodcastForge8" / L"Projects";
}

} // namespace pf8::paths
