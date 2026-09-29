#include "core/SystemStats.h"

#include <windows.h>

namespace pf8 {
namespace {
uint64_t toU64(const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
} // namespace

double CpuMeter::sample()
{
    FILETIME create, exit, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) return 0.0;
    GetSystemTimeAsFileTime(&now);
    const uint64_t proc = toU64(kernel) + toU64(user);
    const uint64_t wall = toU64(now);
    double pct = 0.0;
    if (lastWall_ != 0 && wall > lastWall_)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        const double cores = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
        pct = 100.0 * static_cast<double>(proc - lastProcess_) / (static_cast<double>(wall - lastWall_) * cores);
    }
    lastProcess_ = proc;
    lastWall_ = wall;
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

std::optional<DiskSpace> diskSpace(const std::filesystem::path& path)
{
    std::filesystem::path p = path;
    std::error_code ec;
    while (!p.empty() && !std::filesystem::exists(p, ec))
    {
        const auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    ULARGE_INTEGER freeToCaller, total, totalFree;
    if (!GetDiskFreeSpaceExW(p.c_str(), &freeToCaller, &total, &totalFree)) return std::nullopt;
    return DiskSpace{freeToCaller.QuadPart, total.QuadPart};
}

} // namespace pf8
