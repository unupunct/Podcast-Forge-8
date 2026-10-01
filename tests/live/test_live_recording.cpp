#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <windows.h>

#include <cmath>
#include <cstdio>

#include "TestDirs.h"
#include "core/SettingsDb.h"
#include "engine/EngineController.h"
#include "record/Journal.h"

using namespace pf8;

TEST_CASE("Live: record a real multi-device session to disk", "[live][record]")
{
    pf8test::TempDir dir("live-recording");
    SettingsDb db;
    REQUIRE(db.open(dir.path() / "Settings.db"));
    EngineController c({48000, 128, StreamMode::Shared}, &db);
    c.start();
    c.waitIdle();
    auto find = [&](Flow f, const char* frag) -> std::optional<std::string> {
        for (const auto& d : c.registry().devices())
            if (d.online() && d.raw.flow == f && d.raw.friendlyName.find(frag) != std::string::npos) return d.raw.endpointId;
        return std::nullopt;
    };
    auto mic = find(Flow::Capture, "BRIO");
    auto cableIn = find(Flow::Render, "CABLE Input");
    auto cableOut = find(Flow::Capture, "CABLE Output");
    if (!mic || !cableIn || !cableOut) SKIP("needs a microphone and VB-Audio Virtual Cable");
    c.assignMic(0, *mic);
    c.assignHeadphones(0, *cableIn);
    c.assignMic(1, *cableOut);
    c.setChannelName(0, "Host 1");
    c.setChannelName(1, "Loopback");
    c.waitIdle();
    for (int ch = 2; ch < 8; ++ch) c.engine().recordArm(ch) = false;
    c.recordingSettings().projectDir = dir.path() / "Project";
    Sleep(3000); // let the bridges prime and settle

    std::string err;
    REQUIRE(c.startRecording(err));
    Sleep(2000);
    c.recorder().addMarker("live marker");
    Sleep(3000);
    const auto st = c.recorder().status();
    c.recorder().stop();
    const auto session = st.session;
    std::printf("  session %s  frames=%llu dropped=%llu tracks=%d\n", session.string().c_str(),
                static_cast<unsigned long long>(st.frames), static_cast<unsigned long long>(st.droppedFrames), st.tracks);

    const auto j = Journal::load(session / "Metadata" / "Journal.json");
    REQUIRE(j.has_value());
    CHECK(j->state == "finalised");
    REQUIRE(j->tracks.size() == 3);
    CHECK(st.droppedFrames == 0);
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    uint64_t length = 0;
    for (const auto& t : j->tracks)
    {
        std::unique_ptr<juce::AudioFormatReader> r(mgr.createReaderFor(juce::File(juce::String((session / std::filesystem::path(t.file)).wstring().c_str()))));
        REQUIRE(r != nullptr);
        const auto n = static_cast<uint64_t>(r->lengthInSamples);
        if (length == 0) length = n;
        CHECK(n == length); // every track exactly the same length
        juce::AudioBuffer<float> b(static_cast<int>(r->numChannels), static_cast<int>(n));
        r->read(&b, 0, static_cast<int>(n), 0, true, true);
        const float peak = b.getMagnitude(0, static_cast<int>(n));
        std::printf("  %-28s %8llu frames  peak %.1f dBFS\n", t.file.c_str(), static_cast<unsigned long long>(n),
                    peak > 0 ? 20.0 * std::log10(peak) : -200.0);
        CHECK(peak > 0.0f); // real audio (room noise at least) on every track
    }
    CHECK(length > 48000 * 4); // ~5 s
    CHECK(length < 48000 * 6);
    CHECK(std::filesystem::exists(session / "Metadata" / "Markers.json"));
}
