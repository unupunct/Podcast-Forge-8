#include "record/Session.h"
#include "core/Paths.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace pf8 {

LocalStamp localStamp(int sampleRate)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char d[16], t[16], f[32];
    std::snprintf(d, sizeof d, "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    std::snprintf(t, sizeof t, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    std::snprintf(f, sizeof f, "%04u-%02u-%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const uint64_t ms = ((st.wHour * 60ull + st.wMinute) * 60ull + st.wSecond) * 1000ull + st.wMilliseconds;
    return {d, t, f, ms * static_cast<uint64_t>(sampleRate) / 1000ull};
}

std::optional<std::filesystem::path> createSessionFolder(const std::filesystem::path& projectDir, std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(projectDir, ec);
    const std::string base = "Session_" + localStamp(48000).folderTag;
    for (int n = 1; n < 1000; ++n)
    {
        const auto dir = projectDir / (n == 1 ? base : base + "_" + std::to_string(n));
        if (CreateDirectoryW(dir.c_str(), nullptr))
        {
            for (const wchar_t* sub : {L"Audio", L"Mix", L"Metadata"}) CreateDirectoryW((dir / sub).c_str(), nullptr);
            return dir;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            if (error) *error = "cannot create the session folder in " + paths::utf8(projectDir);
            return std::nullopt;
        }
    }
    if (error) *error = "too many sessions with the same timestamp";
    return std::nullopt;
}

std::string sanitizeFileName(const std::string& name)
{
    std::string s;
    for (unsigned char c : name)
    {
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos) s += '_';
        else s += static_cast<char>(c);
    }
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    std::string upper = s.substr(0, s.find('.'));
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
                                     "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : reserved)
        if (upper == r) s = "_" + s;
    if (s.empty()) s = "Track";
    if (s.size() > 80)
    {
        size_t n = 80;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n; // never split a UTF-8 character
        s.resize(n);
    }
    return s;
}

std::filesystem::path uniqueFilePath(const std::filesystem::path& dir, const std::string& stem, const std::string& ext)
{
    std::error_code ec;
    for (int n = 1; n < 10000; ++n)
    {
        const auto p = dir / paths::fromUtf8(n == 1 ? stem + ext : stem + "_" + std::to_string(n) + ext);
        if (!std::filesystem::exists(p, ec)) return p;
    }
    return dir / paths::fromUtf8(stem + "_overflow" + ext);
}

} // namespace pf8
