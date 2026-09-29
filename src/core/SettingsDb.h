#pragma once
// Persistent application settings: a SQLite key/value store at
// %LOCALAPPDATA%\PodcastForge8\Settings.db. Control/UI threads only. Audio is never stored here.
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

struct sqlite3;

namespace pf8 {

class SettingsDb
{
public:
    SettingsDb() = default;
    ~SettingsDb();
    SettingsDb(const SettingsDb&) = delete;
    SettingsDb& operator=(const SettingsDb&) = delete;

    bool open(const std::filesystem::path& file, std::string* error = nullptr);
    void close();
    bool isOpen() const noexcept { return db_ != nullptr; }

    bool set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key) const;
    bool remove(const std::string& key);

private:
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace pf8
