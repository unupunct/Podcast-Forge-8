#pragma once
// Structured JSON-lines logger.
//
// write() is real-time safe: it formats into a fixed-size event and pushes it onto a lock-free
// queue (dropping and counting the event when the queue is full). A background thread escapes,
// timestamps and writes lines to rotating files. Audio content is never logged.
#include <cstdint>
#include <filesystem>

namespace pf8::log {

enum class Level : uint8_t { Debug, Info, Warn, Error };

struct Config
{
    std::filesystem::path dir;
    uint64_t maxFileBytes = 10ull << 20;
    int maxFiles = 10;
    Level minLevel = Level::Info;
    bool alsoDebugOutput = false; // mirror to OutputDebugString
};

void start(const Config& config);
void stop(); // drains the queue and closes the file; safe to call when not started
bool isRunning() noexcept;
void setMinLevel(Level level) noexcept; // takes effect immediately
Level minLevel() noexcept;

void write(Level level, const char* category, const char* fmt, ...) noexcept;

uint64_t droppedEvents() noexcept;
std::filesystem::path currentFile(); // for diagnostics / tests

} // namespace pf8::log

#define PF8_LOG_DEBUG(cat, ...) ::pf8::log::write(::pf8::log::Level::Debug, cat, __VA_ARGS__)
#define PF8_LOG_INFO(cat, ...)  ::pf8::log::write(::pf8::log::Level::Info, cat, __VA_ARGS__)
#define PF8_LOG_WARN(cat, ...)  ::pf8::log::write(::pf8::log::Level::Warn, cat, __VA_ARGS__)
#define PF8_LOG_ERROR(cat, ...) ::pf8::log::write(::pf8::log::Level::Error, cat, __VA_ARGS__)
