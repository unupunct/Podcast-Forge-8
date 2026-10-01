#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <fstream>

#include "TestDirs.h"
#include "core/Paths.h"
#include "engine/EngineController.h"
#include "project/Hotkeys.h"
#include "project/Project.h"
#include "project/ProjectState.h"

using namespace pf8;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- hotkeys

TEST_CASE("Hotkeys: chords parse and format both ways", "[hotkeys]")
{
    for (const char* s : {"F9", "Ctrl+Shift+F9", "Space", "A", "5", "Num 3", "Alt+F24", "Ctrl+Plus", "PageDown"})
    {
        INFO(s);
        const auto c = parseChord(s);
        REQUIRE(c.has_value());
        CHECK(formatChord(*c) == s);
    }
    CHECK(parseChord("ctrl+shift+f9") == parseChord("Ctrl+Shift+F9"));
    CHECK(parseChord("Numpad7") == parseChord("Num 7"));
    CHECK(parseChord("Ctrl++") == parseChord("Ctrl+Plus"));
    for (const char* bad : {"", "Ctrl+", "Hyper+F1", "F25", "F0", "Ctrl+Shift", "Num 12", "é"}) CHECK_FALSE(parseChord(bad).has_value());
}

TEST_CASE("Hotkeys: defaults, conflicts and JSON round trip", "[hotkeys]")
{
    auto h = HotkeyConfig::defaults();
    CHECK(formatChord(h.find(HotkeyAction::Record)->chord) == "F9");
    CHECK(formatChord(h.find(HotkeyAction::Stop)->chord) == "F10");
    CHECK(formatChord(h.find(HotkeyAction::Pause)->chord) == "F11");
    CHECK(formatChord(h.find(HotkeyAction::Marker)->chord) == "F12");
    CHECK(formatChord(h.find(HotkeyAction::RecordToggle)->chord) == "Space");
    CHECK(h.conflicts().empty());

    h.set(cartAction(3), *parseChord("F9"), false); // clashes with Record
    const auto c = h.conflicts();
    REQUIRE(c.size() == 1);
    CHECK(((c[0].first == HotkeyAction::Record && c[0].second == cartAction(3)) || (c[0].second == HotkeyAction::Record && c[0].first == cartAction(3))));
    h.set(cartAction(3), *parseChord("Ctrl+1"), true);
    h.coughModes[2] = CoughMode::Toggle;
    h.coughModes[5] = CoughMode::PushToTalk;
    CHECK(HotkeyConfig::fromJson(h.toJson()) == h);
    CHECK(HotkeyConfig::fromJson(json::Value()) == HotkeyConfig::defaults());

    for (int i = 0; i < kHotkeyActions; ++i)
    {
        const auto a = static_cast<HotkeyAction>(i);
        CHECK(actionFromId(actionId(a)) == a);
    }
}

TEST_CASE("Hotkeys: dispatcher: press actions, holds, repeats and focus loss", "[hotkeys]")
{
    HotkeyDispatcher d;
    std::vector<std::pair<HotkeyAction, bool>> log;
    d.onAction = [&](HotkeyAction a, bool down) { log.emplace_back(a, down); };
    d.setBindings(HotkeyConfig::defaults().bindings);

    const auto f9 = *parseChord("F9");
    CHECK(d.keyDown(f9));
    CHECK(d.keyDown(f9)); // auto-repeat: consumed, not re-triggered
    CHECK(d.keyUp(f9.vk));
    REQUIRE(log.size() == 1);
    CHECK(log[0] == std::make_pair(HotkeyAction::Record, true));

    // Cough on CH 3 (Num 3) is a hold: down … up.
    log.clear();
    const auto n3 = *parseChord("Num 3");
    d.keyDown(n3);
    d.keyDown(n3);
    d.keyUp(n3.vk);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == std::make_pair(coughAction(2), true));
    CHECK(log[1] == std::make_pair(coughAction(2), false));

    // A hold whose modifier is released first still ends on its key's release.
    HotkeyConfig h = HotkeyConfig::defaults();
    h.set(HotkeyAction::Talkback, *parseChord("Ctrl+T"), false);
    d.setBindings(h.bindings);
    log.clear();
    d.keyDown(*parseChord("Ctrl+T"));
    d.keyDown(*parseChord("T")); // Ctrl released, T still auto-repeating
    d.keyUp('T');
    REQUIRE(log.size() == 2);
    CHECK(log[1] == std::make_pair(HotkeyAction::Talkback, false));

    // Focus lost mid-hold: released, so nobody stays muted or keyed.
    log.clear();
    d.keyDown(n3);
    d.releaseAll();
    REQUIRE(log.size() == 2);
    CHECK(log[1] == std::make_pair(coughAction(2), false));

    CHECK_FALSE(d.keyDown(*parseChord("Q"))); // unbound: not consumed
}

// ---------------------------------------------------------------- project state

TEST_CASE("Project state: capture, apply, capture again is identical", "[project]")
{
    pf8test::TempDir dir("project-state");
    const fs::path music = dir.path() / fs::path(u8"m\u0103r.wav");
    std::ofstream(music) << "x";

    json::Value saved;
    {
        EngineController c(EngineController::Settings{}, nullptr);
        auto& e = c.engine();
        e.dsp(2).compOn = true;
        e.dsp(2).compRatio.set(6.5f);
        e.dsp(2).eq[1].gainDb.set(-4.0f);
        e.dsp(7).hpfHz.set(120.0f);
        e.recordArm(4) = false;
        e.masterDsp().limiterCeilingDb.set(-2.0f);
        auto& rp = e.routing();
        rp.channel[1].fader.set(0.5f);
        rp.channel[1].pan.set(-0.25f);
        rp.channel[6].mute = true;
        rp.headphones[3].mode = HpMode::Custom;
        rp.headphones[3].volume.set(0.6f);
        rp.gain[idx(SourceId::Music)][hpBus(3)].set(0.35f);
        rp.monitor.source = MonitorSource::Hp4;
        rp.talkbackTarget[2] = true;
        rp.talkbackSource = 5;
        c.talkbackKey().setMode(TalkbackMode::Latch);
        c.recordingSettings().format = FileFormat::Flac;
        c.recordingSettings().depth = BitDepth::Int16;
        c.recordingSettings().recordMusic = true;
        REQUIRE(e.setPrerollSeconds(10.0));
        e.soundboard().settings(5).name = "Jingle";
        e.soundboard().settings(5).loop = true;
        e.soundboard().settings(5).volume.set(0.7f);
        e.soundboard().settings(5).hotkey = "Ctrl+5";
        e.music().setPlaylist({music});
        e.music().volume().set(0.4f);
        e.ducker().depthDb.set(-20.0f);
        saved = project::captureState(c);
    }
    EngineController fresh(EngineController::Settings{}, nullptr);
    const auto r = project::applyState(saved, fresh);
    CHECK(r.warnings.empty());
    CHECK(json::serialize(project::captureState(fresh)) == json::serialize(saved));
    CHECK(fresh.engine().dsp(2).compRatio.get() == 6.5f);
    CHECK(fresh.engine().routing().monitor.source.load() == MonitorSource::Hp4);
    CHECK(fresh.talkbackKey().mode() == TalkbackMode::Latch);
    CHECK(fresh.engine().preroll() != nullptr);
    REQUIRE(fresh.engine().music().playlist().size() == 1);
    CHECK(fresh.engine().music().playlist()[0].path == music);
}

TEST_CASE("Project state: garbage values are ignored, missing files reported", "[project]")
{
    EngineController c(EngineController::Settings{}, nullptr);
    auto state = project::captureState(c);
    auto& root = state.object();
    root["dsp"].array()[0].object()["hpfHz"] = "loud";
    root["dsp"].array()[0].object()["compDetector"] = 9;
    root["routing"].object()["monitor"].object()["source"] = -3;
    root["carts"].array()[0].object()["file"] = "C:/definitely/not/here.wav";
    root["recording"].object()["prerollSeconds"] = 7.0; // not an allowed length
    const auto r = project::applyState(state, c);
    CHECK(c.engine().dsp(0).hpfHz.get() == 80.0f);
    CHECK(c.engine().dsp(0).compDetector.load() == DetectorMode::Rms);
    CHECK(c.engine().routing().monitor.source.load() == MonitorSource::Main);
    CHECK(c.engine().preroll() == nullptr);
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].find("Cart 1") != std::string::npos);
    CHECK(project::applyState(json::Value("nonsense"), c).carts.empty());
}

// ---------------------------------------------------------------- project files

TEST_CASE("Project files: create, save atomically, reopen, never reuse a folder", "[project]")
{
    pf8test::TempDir dir("project-files");
    json::Object st;
    st["x"] = 1;
    std::string err;
    const auto a = project::createProject(dir.path(), "Emisiune \xC8\x99" "edin\xC8\x9B\xC4\x83", json::Value(st), err);
    REQUIRE(a.has_value());
    CHECK(project::isProjectDir(*a));
    const auto b = project::createProject(dir.path(), "Emisiune \xC8\x99" "edin\xC8\x9B\xC4\x83", json::Value(st), err);
    REQUIRE(b.has_value());
    CHECK(*a != *b); // a second project with the same name gets its own folder
    CHECK(paths::utf8(b->filename()).find("(2)") != std::string::npos);

    // A session folder inside is listed and survives saving.
    fs::create_directories(*a / "Session_2026-10-01_120000" / "Metadata");
    st["x"] = 2;
    REQUIRE(project::saveProject(*a, "Renamed", json::Value(st), err));
    CHECK_FALSE(fs::exists(*a / "Project.json.saving"));
    auto p = project::loadProject(*a, err);
    REQUIRE(p.has_value());
    CHECK(p->name == "Renamed");
    CHECK(p->state["x"].asInt() == 2);
    CHECK_FALSE(p->createdUtc.empty());
    CHECK(project::listSessions(*a).size() == 1);
    CHECK(fs::exists(*a / "Session_2026-10-01_120000"));

    // Not a project.
    std::ofstream(dir.path() / "Project.json") << "{\"format\":\"something else\"}";
    CHECK_FALSE(project::loadProject(dir.path(), err).has_value());
    CHECK(project::sanitizeProjectName("  a:b?  ") == "a_b_");
    CHECK(project::sanitizeProjectName("") == "Untitled Project");
}

TEST_CASE("Project archive: zips everything, refuses to overwrite", "[project]")
{
    pf8test::TempDir dir("project-archive");
    std::string err;
    const auto p = project::createProject(dir.path(), "Show", json::Value(json::Object{}), err);
    REQUIRE(p.has_value());
    fs::create_directories(*p / "Session_1" / "Audio");
    {
        std::ofstream f(*p / "Session_1" / "Audio" / "CH01_Host.wav", std::ios::binary);
        for (int i = 0; i < 100000; ++i) f.put(static_cast<char>(i * 31));
    }
    const fs::path zip = dir.path() / "Show.zip";
    REQUIRE(project::archiveProject(*p, zip, err));
    juce::ZipFile z(juce::File(juce::String(zip.wstring().c_str())));
    CHECK(z.getNumEntries() == 2);
    CHECK(z.getEntry("Show/Project.json") != nullptr);
    const auto* wav = z.getEntry("Show/Session_1/Audio/CH01_Host.wav");
    REQUIRE(wav != nullptr);
    CHECK(wav->uncompressedSize == 100000);
    CHECK_FALSE(project::archiveProject(*p, zip, err)); // exists: never overwritten
    CHECK(fs::exists(*p / "Session_1" / "Audio" / "CH01_Host.wav")); // source untouched
}
