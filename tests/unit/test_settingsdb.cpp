#include <catch2/catch_test_macros.hpp>

#include "TestDirs.h"
#include "core/SettingsDb.h"

TEST_CASE("SettingsDb stores, overwrites, removes and persists values", "[core][settings]")
{
    pf8test::TempDir dir("settingsdb");
    const auto file = dir.path() / "Settings.db";
    {
        pf8::SettingsDb db;
        REQUIRE(db.open(file));
        CHECK_FALSE(db.get("missing").has_value());
        CHECK(db.set("assignments", "{\"a\":1}"));
        CHECK(db.set("assignments", "{\"a\":2}"));
        CHECK(db.set("unicode", "Gazd\xc4\x83 \xf0\x9f\x8e\x99"));
        CHECK(db.get("assignments") == "{\"a\":2}");
        CHECK(db.set("temp", "x"));
        CHECK(db.remove("temp"));
        CHECK_FALSE(db.get("temp").has_value());
    }
    pf8::SettingsDb again;
    REQUIRE(again.open(file));
    CHECK(again.get("assignments") == "{\"a\":2}");
    CHECK(again.get("unicode") == "Gazd\xc4\x83 \xf0\x9f\x8e\x99");
}

#include "project/AppSettings.h"

TEST_CASE("AppSettings round trip; invalid stored values fall back to defaults", "[core][settings]")
{
    pf8test::TempDir dir("appsettings");
    pf8::SettingsDb db;
    REQUIRE(db.open(dir.path() / "Settings.db"));
    CHECK(pf8::AppSettings::load(&db) == pf8::AppSettings{});
    CHECK(pf8::AppSettings::load(nullptr) == pf8::AppSettings{});
    pf8::AppSettings s;
    s.sampleRate = 96000;
    s.blockFrames = 256;
    s.mode = pf8::StreamMode::Exclusive;
    s.uiScale = 1.5;
    s.debugLog = true;
    REQUIRE(s.save(&db));
    CHECK(pf8::AppSettings::load(&db) == s);
    db.set("audio.sampleRate", "12345");
    db.set("audio.blockFrames", "abc");
    db.set("audio.mode", "7");
    db.set("ui.scalePercent", "900");
    const auto bad = pf8::AppSettings::load(&db);
    CHECK(bad.sampleRate == 48000);
    CHECK(bad.blockFrames == 128);
    CHECK(bad.mode == pf8::StreamMode::Shared);
    CHECK(bad.uiScale == 1.0);
}
