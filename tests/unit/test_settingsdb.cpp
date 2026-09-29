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
