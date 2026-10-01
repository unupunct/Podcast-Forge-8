#pragma once
// Disk-space estimates and warning levels (RECORDING.md §6). Warns; never stops a recording.
#include <cstdint>
#include <filesystem>
#include <optional>

namespace pf8 {

enum class DiskLevel : uint8_t { Ok, Low, Amber, Red };
const char* toString(DiskLevel l) noexcept;

struct DiskEstimate
{
    uint64_t freeBytes = 0;
    double bytesPerSecond = 0.0;
    double secondsRemaining = 0.0;
    DiskLevel level = DiskLevel::Ok;
};

class DiskGuard
{
public:
    static constexpr double kPreRecordWarnSeconds = 30 * 60; // "Low": warn before starting
    static constexpr double kAmberSeconds = 10 * 60;
    static constexpr double kRedSeconds = 2 * 60;

    static double bytesPerSecond(int monoTracks, int stereoTracks, int sampleRate, int bytesPerSample);
    static DiskLevel levelFor(double secondsRemaining) noexcept;
    static DiskEstimate estimate(const std::filesystem::path& dir, double bytesPerSecond);
};

} // namespace pf8
