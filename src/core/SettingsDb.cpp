#include "core/SettingsDb.h"

#include <sqlite3.h>

#include "core/Log.h"

namespace pf8 {

SettingsDb::~SettingsDb() { close(); }

bool SettingsDb::open(const std::filesystem::path& file, std::string* error)
{
    std::lock_guard lock(mutex_);
    if (db_) return true;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto u8 = file.u8string();
    const std::string utf8(u8.begin(), u8.end());
    if (sqlite3_open_v2(utf8.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        if (error) *error = db_ ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
        PF8_LOG_ERROR("settings", "cannot open settings db: %s", db_ ? sqlite3_errmsg(db_) : "?");
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db_, 2000);
    const char* schema =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS kv (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated TEXT NOT NULL DEFAULT (datetime('now')));";
    char* msg = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &msg) != SQLITE_OK)
    {
        if (error) *error = msg ? msg : "schema failed";
        sqlite3_free(msg);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    return true;
}

void SettingsDb::close()
{
    std::lock_guard lock(mutex_);
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

bool SettingsDb::set(const std::string& key, const std::string& value)
{
    std::lock_guard lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "INSERT INTO kv(key,value,updated) VALUES(?,?,datetime('now')) "
                                "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated=excluded.updated;",
                           -1, &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::optional<std::string> SettingsDb::get(const std::string& key) const
{
    std::lock_guard lock(mutex_);
    if (!db_) return std::nullopt;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value FROM kv WHERE key=?;", -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> out;
    if (sqlite3_step(st) == SQLITE_ROW)
    {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        out = text ? std::string(text) : std::string();
    }
    sqlite3_finalize(st);
    return out;
}

bool SettingsDb::remove(const std::string& key)
{
    std::lock_guard lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM kv WHERE key=?;", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

} // namespace pf8
