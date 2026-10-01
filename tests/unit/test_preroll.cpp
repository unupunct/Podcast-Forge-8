#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <windows.h>

#include <atomic>
#include <cmath>
#include <thread>

#include "EngineHarness.h"
#include "TestDirs.h"
#include "core/RealtimeGuard.h"
#include "record/Journal.h"
#include "record/PreRollBuffer.h"
#include "record/Recorder.h"

using namespace pf8;
namespace fs = std::filesystem;

namespace {
constexpr int kRate = 48000;

// Sample value encoding the global frame counter: exactly representable, steps of 2^-13.
float counterValue(uint64_t frame, int track) { return static_cast<float>((frame + 97u * static_cast<uint64_t>(track)) % 4096u) / 8192.0f; }

// Emulates the engine tick: every block goes to the pre-roll buffer and (when active) the tap,
// with the same single read of the tap state the engine uses.
class FakeTick
{
public:
    FakeTick(RecordTap& tap, PreRollBuffer& pre) : tap_(tap), pre_(pre)
    {
        for (auto& b : mono_) b.resize(kBlock);
        stereo_.resize(2 * kBlock);
        thread_ = std::thread([this] { run(); });
    }
    ~FakeTick()
    {
        quit_ = true;
        thread_.join();
    }
    uint64_t frames() const { return frame_.load(); }
    void waitFor(uint64_t frames) const
    {
        while (frame_.load() < frames) Sleep(1);
    }

private:
    static constexpr int kBlock = 480;
    void run()
    {
        while (!quit_)
        {
            const uint64_t f0 = frame_.load();
            std::array<const float*, kTrackCount> tracks{};
            for (int t = 0; t < 8; ++t)
            {
                for (int i = 0; i < kBlock; ++i) mono_[static_cast<size_t>(t)][static_cast<size_t>(i)] = counterValue(f0 + static_cast<uint64_t>(i), t);
                tracks[static_cast<size_t>(t)] = mono_[static_cast<size_t>(t)].data();
            }
            for (int i = 0; i < kBlock; ++i)
            {
                stereo_[static_cast<size_t>(2 * i)] = counterValue(f0 + static_cast<uint64_t>(i), 8);
                stereo_[static_cast<size_t>(2 * i + 1)] = -counterValue(f0 + static_cast<uint64_t>(i), 8);
            }
            tracks[static_cast<size_t>(TrackId::Main)] = stereo_.data();
            const bool recording = tap_.active();
            pre_.onBlock(tracks, kBlock, recording);
            if (recording) tap_.push(tracks, kBlock);
            frame_.fetch_add(kBlock);
            Sleep(1); // ~10x real time
        }
    }
    RecordTap& tap_;
    PreRollBuffer& pre_;
    std::array<std::vector<float>, 8> mono_;
    std::vector<float> stereo_;
    std::atomic<uint64_t> frame_{0};
    std::atomic<bool> quit_{false};
    std::thread thread_;
};

std::unique_ptr<juce::AudioFormatReader> openReader(const fs::path& p)
{
    static juce::AudioFormatManager mgr;
    static bool init = [] {
        mgr.registerBasicFormats();
        return true;
    }();
    (void)init;
    return std::unique_ptr<juce::AudioFormatReader>(mgr.createReaderFor(juce::File(juce::String(p.wstring().c_str()))));
}

// Every sample of the file continues the counter: no gap, no repeat at the pre-roll → live joint.
void checkContiguous(const fs::path& file, int track, int channels)
{
    auto r = openReader(file);
    REQUIRE(r != nullptr);
    const int n = static_cast<int>(r->lengthInSamples);
    REQUIRE(n > kRate);
    juce::AudioBuffer<float> b(channels, n);
    r->read(&b, 0, n, 0, true, channels > 1);
    // Recover the start frame from the first sample, then demand an exact match everywhere.
    const auto first = static_cast<uint64_t>(std::lround(b.getSample(0, 0) * 8192.0f));
    int bad = 0;
    for (int i = 0; i < n && bad < 5; ++i)
    {
        const float want = static_cast<float>((first + static_cast<uint64_t>(i)) % 4096u) / 8192.0f;
        if (b.getSample(0, i) != want)
        {
            ++bad;
            UNSCOPED_INFO(file.filename().string() << " sample " << i << " got " << b.getSample(0, i) << " want " << want);
        }
        if (channels > 1 && b.getSample(1, i) != -want) ++bad;
    }
    (void)track;
    CHECK(bad == 0);
}
} // namespace

TEST_CASE("PreRollBuffer: wraps, freezes on the first recorded block, releases empty", "[record][preroll]")
{
    PreRollBuffer p(1000, 1.0); // 1000 frames
    std::vector<float> x(300);
    std::array<const float*, kTrackCount> tracks{};
    tracks[0] = x.data();
    uint64_t f = 0;
    auto block = [&](bool rec) {
        for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(f + i);
        p.onBlock(tracks, 300, rec);
        if (!rec || !p.frozen()) f += 300;
    };
    for (int i = 0; i < 5; ++i) block(false); // 1500 frames written, 1000 kept
    CHECK(p.filled() == 1000);
    block(true); // not armed: a recording without pre-roll leaves it alone
    CHECK_FALSE(p.frozen());
    p.armCapture();
    block(true);
    REQUIRE(p.frozen());
    CHECK(p.frozenFrames() == 1000);
    std::vector<float> out(1000);
    p.read(0, 0, 1000, out.data());
    for (int i = 0; i < 1000; ++i) REQUIRE(out[static_cast<size_t>(i)] == static_cast<float>(500 + i)); // the newest 1000 frames
    block(true); // frozen: further blocks don't touch it
    p.read(0, 999, 1, out.data());
    CHECK(out[0] == 1499.0f);
    p.release();
    CHECK_FALSE(p.frozen());
    CHECK(p.filled() == 0);
    block(false);
    CHECK(p.filled() == 300);
}

TEST_CASE("Pre-roll recording is sample-contiguous with the live stream", "[record][preroll]")
{
    pf8test::TempDir dir("preroll");
    RecordTap tap;
    PreRollBuffer pre(kRate, 5.0);
    Recorder rec(tap, kRate);
    rec.setPrerollSource(&pre);
    FakeTick tick(tap, pre);
    tick.waitFor(static_cast<uint64_t>(kRate) * 7); // more than the 5 s buffer

    Recorder::Settings s;
    s.projectDir = dir.path();
    s.depth = BitDepth::Float32;
    s.armed = {true, true, false, false, false, false, false, false};
    std::string err;
    REQUIRE(rec.start(s, err));
    CHECK(rec.prerollFrames() == static_cast<uint64_t>(kRate) * 5);
    const uint64_t at = tick.frames();
    tick.waitFor(at + static_cast<uint64_t>(kRate) * 3);
    rec.stop();
    CHECK(tap.framesDropped() == 0);

    const auto session = rec.status().session;
    const auto journal = Journal::load(session / "Metadata" / "Journal.json");
    REQUIRE(journal.has_value());
    CHECK(journal->prerollSamples == static_cast<uint64_t>(kRate) * 5);
    for (const auto& t : journal->tracks)
    {
        INFO(t.file);
        CHECK(t.samplesWritten > static_cast<uint64_t>(kRate) * 8 - 1000);
        const bool main = t.name == "Main Mix";
        checkContiguous(session / fs::path(t.file), main ? 8 : 0, main ? 2 : 1);
    }
    const auto markers = rec.markers().all();
    REQUIRE_FALSE(markers.empty());
    CHECK(markers.front().samplePos == static_cast<uint64_t>(kRate) * 5);
    CHECK(markers.front().label == "Record pressed");

    // A second take shortly after: the buffer restarted empty at stop, so it holds only what came since.
    tick.waitFor(tick.frames() + static_cast<uint64_t>(kRate));
    REQUIRE(rec.start(s, err));
    CHECK(rec.prerollFrames() >= static_cast<uint64_t>(kRate));
    CHECK(rec.prerollFrames() < static_cast<uint64_t>(kRate) * 5);
    rec.stop();
    for (const auto& t : Journal::load(rec.status().session / "Metadata" / "Journal.json")->tracks)
        checkContiguous(rec.status().session / fs::path(t.file), 0, t.name == "Main Mix" ? 2 : 1);
}

TEST_CASE("Engine feeds the pre-roll buffer without real-time allocations", "[record][preroll][harness]")
{
    pf8test::EngineHarness h(48000, 128);
    pf8test::FakeDeviceSpec hp;
    hp.master = true;
    hp.channels = 2;
    h.addOutput(hp);
    REQUIRE(h.engine().setPrerollSeconds(10.0));
    REQUIRE(h.engine().preroll() != nullptr);
    CHECK(h.engine().preroll()->capacity() == 480000);
    h.commitGraph();
    h.run(3.0);
    const uint64_t filled = h.engine().preroll()->filled();
    CHECK(filled > 48000 * 3 - 2000);
    CHECK(filled <= 48000 * 3 + 2000);
    CHECK(h.realtimeAllocations() == 0);
    REQUIRE(h.engine().setPrerollSeconds(0.0));
    CHECK(h.engine().preroll() == nullptr);
    h.run(0.2); // the tick runs fine without it
    CHECK(PreRollBuffer::bytesFor(48000, 60.0) == static_cast<size_t>(48000) * 60 * 12 * 4);
}
