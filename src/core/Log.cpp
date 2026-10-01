#include "core/Log.h"
#include "core/Paths.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "core/MpscQueue.h"

namespace pf8::log {
namespace {

struct Event
{
    int64_t fileTime = 0; // 100 ns since 1601 UTC
    uint32_t threadId = 0;
    Level level = Level::Info;
    char category[24]{};
    char message[456]{};
};

const char* levelName(Level l) noexcept
{
    switch (l)
    {
        case Level::Debug: return "debug";
        case Level::Info:  return "info";
        case Level::Warn:  return "warn";
        case Level::Error: return "error";
    }
    return "info";
}

void appendEscaped(std::string& out, const char* s)
{
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p)
    {
        const unsigned char c = *p;
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c); // UTF-8 passes through unchanged
                }
        }
    }
}

std::string formatTimestamp(int64_t fileTime)
{
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(fileTime & 0xffffffff);
    ft.dwHighDateTime = static_cast<DWORD>(fileTime >> 32);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

std::string todayStamp()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}

const std::regex& logNamePattern()
{
    static const std::regex re(R"(^podcastforge8-(\d{8})-(\d+)\.jsonl$)");
    return re;
}

class Logger
{
public:
    Logger() : queue_(4096) {}

    void start(const Config& cfg)
    {
        std::lock_guard lock(controlMutex_);
        stopLocked();
        cfg_ = cfg;
        minLevel_.store(cfg_.minLevel);
        std::error_code ec;
        std::filesystem::create_directories(cfg_.dir, ec);
        openNextFile();
        running_.store(true);
        thread_ = std::thread([this] { run(); });
    }

    void stop()
    {
        std::lock_guard lock(controlMutex_);
        stopLocked();
    }

    bool running() const noexcept { return running_.load(); }

    void push(Level level, const char* category, const char* fmt, va_list args) noexcept
    {
        if (!running_.load(std::memory_order_relaxed)) return;
        if (static_cast<int>(level) < static_cast<int>(minLevel_.load(std::memory_order_relaxed))) return;

        Event e;
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        e.fileTime = (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        e.threadId = GetCurrentThreadId();
        e.level = level;
        strncpy_s(e.category, category ? category : "", _TRUNCATE);
        _vsnprintf_s(e.message, sizeof e.message, _TRUNCATE, fmt, args);
        if (!queue_.tryPush(e)) dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t dropped() const noexcept { return dropped_.load(); }

    std::filesystem::path current()
    {
        std::lock_guard lock(fileMutex_);
        return currentPath_;
    }

private:
    void stopLocked()
    {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
        drain();
        std::lock_guard lock(fileMutex_);
        if (file_) { std::fclose(file_); file_ = nullptr; }
    }

    void run()
    {
        while (running_.load())
        {
            if (!drain()) Sleep(20);
        }
    }

    bool drain()
    {
        bool any = false;
        Event e;
        std::string line;
        while (queue_.tryPop(e))
        {
            any = true;
            line.clear();
            line += "{\"ts\":\"";
            line += formatTimestamp(e.fileTime);
            line += "\",\"lvl\":\"";
            line += levelName(e.level);
            line += "\",\"cat\":\"";
            appendEscaped(line, e.category);
            line += "\",\"tid\":";
            line += std::to_string(e.threadId);
            line += ",\"msg\":\"";
            appendEscaped(line, e.message);
            line += "\"}\n";
            writeLine(line);
            if (cfg_.alsoDebugOutput) OutputDebugStringA(line.c_str());
        }
        if (any)
        {
            std::lock_guard lock(fileMutex_);
            if (file_) std::fflush(file_);
        }
        return any;
    }

    void writeLine(const std::string& line)
    {
        std::lock_guard lock(fileMutex_);
        if (file_ && bytesInFile_ > 0 && bytesInFile_ + line.size() > cfg_.maxFileBytes)
        {
            std::fclose(file_);
            file_ = nullptr;
            openNextFileLocked();
        }
        if (!file_) return;
        std::fwrite(line.data(), 1, line.size(), file_);
        bytesInFile_ += line.size();
    }

    void openNextFile()
    {
        std::lock_guard lock(fileMutex_);
        openNextFileLocked();
    }

    void openNextFileLocked()
    {
        const std::string day = todayStamp();
        int maxIndex = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(cfg_.dir, ec))
        {
            std::smatch m;
            const std::string name = paths::utf8(entry.path().filename());
            if (std::regex_match(name, m, logNamePattern()) && m[1] == day)
                maxIndex = std::max(maxIndex, std::stoi(m[2]));
        }
        currentPath_ = cfg_.dir / ("podcastforge8-" + day + "-" + std::to_string(maxIndex + 1) + ".jsonl");
        _wfopen_s(&file_, currentPath_.c_str(), L"ab");
        bytesInFile_ = 0;
        pruneOldFiles();
    }

    // Deletes only this application's own log files, oldest first, beyond maxFiles.
    void pruneOldFiles()
    {
        struct Item { std::string day; int index; std::filesystem::path path; };
        std::vector<Item> items;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(cfg_.dir, ec))
        {
            if (!entry.is_regular_file()) continue;
            std::smatch m;
            const std::string name = paths::utf8(entry.path().filename());
            if (std::regex_match(name, m, logNamePattern()))
                items.push_back({m[1], std::stoi(m[2]), entry.path()});
        }
        if (static_cast<int>(items.size()) <= cfg_.maxFiles) return;
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.day != b.day ? a.day < b.day : a.index < b.index;
        });
        const size_t excess = items.size() - static_cast<size_t>(cfg_.maxFiles);
        for (size_t i = 0; i < excess; ++i)
            if (items[i].path != currentPath_) std::filesystem::remove(items[i].path, ec);
    }

    MpscQueue<Event> queue_;
    std::atomic<bool> running_{false};
    std::atomic<Level> minLevel_{Level::Info};
    std::atomic<uint64_t> dropped_{0};
    std::thread thread_;
    std::mutex controlMutex_;
    std::mutex fileMutex_;
    Config cfg_;
    FILE* file_ = nullptr;
    uint64_t bytesInFile_ = 0;
    std::filesystem::path currentPath_;
};

Logger& logger()
{
    static Logger instance;
    return instance;
}

} // namespace

void start(const Config& config) { logger().start(config); }
void stop() { logger().stop(); }
bool isRunning() noexcept { return logger().running(); }

void write(Level level, const char* category, const char* fmt, ...) noexcept
{
    va_list args;
    va_start(args, fmt);
    logger().push(level, category, fmt, args);
    va_end(args);
}

uint64_t droppedEvents() noexcept { return logger().dropped(); }
std::filesystem::path currentFile() { return logger().current(); }

} // namespace pf8::log
