#pragma once
// Per-test scratch directories under the build tree (never outside the project).
#include <atomic>
#include <filesystem>
#include <string>

#ifndef PF8_TEST_SCRATCH
#error "PF8_TEST_SCRATCH must be defined by the build"
#endif

namespace pf8test {

inline std::filesystem::path scratchRoot() { return std::filesystem::path(PF8_TEST_SCRATCH); }

class TempDir
{
public:
    explicit TempDir(const std::string& name)
    {
        static std::atomic<int> counter{0};
        path_ = scratchRoot() / (name + "-" + std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec); // scratch dirs are test-owned
        std::filesystem::create_directories(path_);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace pf8test
