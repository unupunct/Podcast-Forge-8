#pragma once
// Process CPU usage and disk space helpers (UI/control threads only).
#include <cstdint>
#include <filesystem>
#include <optional>

namespace pf8 {

class CpuMeter
{
public:
    // Percentage of total machine CPU used by this process since the previous call (0–100).
    double sample();

private:
    uint64_t lastProcess_ = 0;
    uint64_t lastWall_ = 0;
};

struct DiskSpace
{
    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
};

// Walks up to the nearest existing ancestor so a not-yet-created project dir still reports.
std::optional<DiskSpace> diskSpace(const std::filesystem::path& path);

} // namespace pf8
