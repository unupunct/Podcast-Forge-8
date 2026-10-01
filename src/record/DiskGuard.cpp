#include "record/DiskGuard.h"

#include "core/SystemStats.h"

namespace pf8 {

const char* toString(DiskLevel l) noexcept
{
    switch (l)
    {
        case DiskLevel::Ok: return "OK";
        case DiskLevel::Low: return "LOW";
        case DiskLevel::Amber: return "LOW - less than 10 minutes";
        case DiskLevel::Red: return "CRITICAL - less than 2 minutes";
    }
    return "?";
}

double DiskGuard::bytesPerSecond(int monoTracks, int stereoTracks, int sampleRate, int bytesPerSample)
{
    return static_cast<double>(monoTracks + 2 * stereoTracks) * sampleRate * bytesPerSample;
}

DiskLevel DiskGuard::levelFor(double s) noexcept
{
    if (s < kRedSeconds) return DiskLevel::Red;
    if (s < kAmberSeconds) return DiskLevel::Amber;
    if (s < kPreRecordWarnSeconds) return DiskLevel::Low;
    return DiskLevel::Ok;
}

DiskEstimate DiskGuard::estimate(const std::filesystem::path& dir, double bps)
{
    DiskEstimate e;
    e.bytesPerSecond = bps;
    if (auto d = diskSpace(dir))
    {
        e.freeBytes = d->freeBytes;
        e.secondsRemaining = bps > 0 ? static_cast<double>(d->freeBytes) / bps : 1e12;
    }
    e.level = levelFor(e.secondsRemaining);
    return e;
}

} // namespace pf8
