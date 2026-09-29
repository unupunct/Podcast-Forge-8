#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestDirs.h"
#include "core/Log.h"
#include "core/RealtimeGuard.h"

namespace fs = std::filesystem;

namespace {
std::vector<std::string> readLines(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

std::vector<fs::path> logFiles(const fs::path& dir)
{
    std::vector<fs::path> out;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".jsonl") out.push_back(e.path());
    return out;
}
} // namespace

TEST_CASE("Logger writes JSON lines with the documented fields", "[core][log]")
{
    pf8test::TempDir dir("log-basic");
    pf8::log::start({dir.path()});
    PF8_LOG_INFO("device", "connected %s", "USB Mic");
    PF8_LOG_WARN("engine", "xrun %d", 3);
    PF8_LOG_ERROR("record", "disk full");
    const fs::path file = pf8::log::currentFile();
    pf8::log::stop();

    auto lines = readLines(file);
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].rfind("{\"ts\":\"", 0) == 0);
    REQUIRE(lines[0].find("\"lvl\":\"info\"") != std::string::npos);
    REQUIRE(lines[0].find("\"cat\":\"device\"") != std::string::npos);
    REQUIRE(lines[0].find("\"msg\":\"connected USB Mic\"") != std::string::npos);
    REQUIRE(lines[1].find("\"lvl\":\"warn\"") != std::string::npos);
    REQUIRE(lines[2].find("\"tid\":") != std::string::npos);
}

TEST_CASE("Logger escapes quotes, backslashes, control characters", "[core][log]")
{
    pf8test::TempDir dir("log-escape");
    pf8::log::start({dir.path()});
    PF8_LOG_INFO("t", "a\"b\\c\nd\x01" "e \xc8\x99");
    const fs::path file = pf8::log::currentFile();
    pf8::log::stop();
    auto lines = readLines(file);
    REQUIRE(lines.size() == 1);
    const std::string expected = std::string("\"msg\":\"a\\\"b\\\\c\\nd\\u0001e ") + "\xc8\x99" + "\"";
    REQUIRE(lines[0].find(expected) != std::string::npos);
}

TEST_CASE("Logger rotates and keeps maxFiles, never touching foreign files", "[core][log]")
{
    pf8test::TempDir dir("log-rotate");
    { std::ofstream(dir.path() / "keep-me.jsonl") << "x"; }
    { std::ofstream(dir.path() / "notes.txt") << "x"; }
    pf8::log::Config cfg{dir.path()};
    cfg.maxFileBytes = 2048;
    cfg.maxFiles = 3;
    pf8::log::start(cfg);
    for (int i = 0; i < 200; ++i)
    {
        PF8_LOG_INFO("rot", "line number %04d padding padding padding", i);
        if (i % 20 == 0) Sleep(30); // let the writer thread rotate between bursts
    }
    pf8::log::stop();
    const fs::path last = pf8::log::currentFile();

    int ours = 0;
    for (auto& p : logFiles(dir.path()))
        if (p.filename().string().rfind("podcastforge8-", 0) == 0) ++ours;
    REQUIRE(ours == 3);
    REQUIRE(fs::exists(dir.path() / "keep-me.jsonl"));
    REQUIRE(fs::exists(dir.path() / "notes.txt"));
    auto lines = readLines(last);
    REQUIRE_FALSE(lines.empty());
    REQUIRE(lines.back().find("line number 0199") != std::string::npos);
}

TEST_CASE("Logger write does not allocate on a realtime thread", "[core][log][rt]")
{
    pf8test::TempDir dir("log-rt");
    pf8::log::start({dir.path()});
    pf8::rt::resetCounters();
    {
        pf8::rt::ScopedRealtime rt;
        for (int i = 0; i < 100; ++i) PF8_LOG_WARN("rt", "underrun device=%d fill=%.2f", i, 0.5 * i);
    }
    REQUIRE(pf8::rt::allocationsOnRealtimeThreads() == 0);
    pf8::log::stop();
}
