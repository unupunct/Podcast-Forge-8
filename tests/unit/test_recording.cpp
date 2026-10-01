#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <windows.h>

#include <atomic>
#include <cmath>
#include <fstream>
#include <map>
#include <thread>

#include "Analysis.h"
#include "EngineHarness.h"
#include "TestDirs.h"
#include "core/RealtimeGuard.h"
#include "record/FileSink.h"
#include "record/Journal.h"
#include "record/Markers.h"
#include "record/Recorder.h"
#include "record/Recovery.h"
#include "record/Session.h"
#include "record/WavWriter.h"

using namespace pf8;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 48000;

std::vector<uint8_t> readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool contains(const std::vector<uint8_t>& hay, const std::string& needle)
{
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

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

// Deterministic per-track test signal (distinct frequency per track).
float signal(int track, int ch, uint64_t frame)
{
    const double f = 200.0 + 110.0 * track + 37.0 * ch;
    return static_cast<float>(0.5 * std::sin(2.0 * pf8test::kPi * f * static_cast<double>(frame) / kRate));
}

// Pushes `frames` frames of the test signals into the tap in 256-frame blocks, like the tick.
void pushFrames(RecordTap& tap, uint64_t& frame, int frames, uint32_t mask)
{
    std::array<std::vector<float>, kTrackCount> buf;
    for (int t = 0; t < kTrackCount; ++t) buf[static_cast<size_t>(t)].resize(256 * 2);
    int left = frames;
    while (left > 0)
    {
        const int n = std::min(256, left);
        std::array<const float*, kTrackCount> ptr{};
        for (int t = 0; t < kTrackCount; ++t)
        {
            if (!(mask & (1u << t))) continue;
            const int ch = trackChannels(t);
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c) buf[static_cast<size_t>(t)][static_cast<size_t>(i * ch + c)] = signal(t, c, frame + static_cast<uint64_t>(i));
            ptr[static_cast<size_t>(t)] = buf[static_cast<size_t>(t)].data();
        }
        while (!tap.push(ptr, n)) std::this_thread::sleep_for(std::chrono::milliseconds(2)); // tests: wait instead of dropping
        frame += static_cast<uint64_t>(n);
        left -= n;
    }
}

Recorder::Settings settings(const fs::path& project, FileFormat f, BitDepth d)
{
    Recorder::Settings s;
    s.projectDir = project;
    s.format = f;
    s.depth = d;
    s.armed = {true, true, false, false, false, false, false, false};
    s.names = {"Host \"One\"", "Guest/Two", "", "", "", "", "", ""};
    s.description = "Episode 12";
    return s;
}

// A sink that refuses appends once a shared byte budget is exhausted, except under `freeDir`.
struct Budget
{
    std::atomic<int64_t> bytes{0};
    fs::path freeDir;
};

class FaultySink : public IFileSink
{
public:
    explicit FaultySink(Budget& b) : budget_(b) {}
    SinkError create(const fs::path& p) override
    {
        unlimited_ = !budget_.freeDir.empty() && p.wstring().rfind(budget_.freeDir.wstring(), 0) == 0;
        return inner_.create(p);
    }
    SinkError append(const void* d, size_t n, size_t& written) override
    {
        if (!unlimited_)
        {
            const int64_t left = budget_.bytes.load();
            if (left <= 0)
            {
                written = 0;
                return SinkError::DiskFull;
            }
            if (static_cast<int64_t>(n) > left)
            {
                const auto e = inner_.append(d, static_cast<size_t>(left), written);
                budget_.bytes -= static_cast<int64_t>(written);
                return e == SinkError::None ? SinkError::DiskFull : e;
            }
            budget_.bytes -= static_cast<int64_t>(n);
        }
        return inner_.append(d, n, written);
    }
    SinkError writeAt(uint64_t o, const void* d, size_t n) override { return inner_.writeAt(o, d, n); }
    SinkError flush() override { return inner_.flush(); }
    void close() override { inner_.close(); }
    uint64_t size() const override { return inner_.size(); }
    const fs::path& path() const override { return inner_.path(); }

private:
    Budget& budget_;
    Win32FileSink inner_;
    bool unlimited_ = false;
};

class FaultyFactory : public SinkFactory
{
public:
    explicit FaultyFactory(Budget& b) : b_(b) {}
    std::unique_ptr<IFileSink> make() override { return std::make_unique<FaultySink>(b_); }

private:
    Budget& b_;
};

} // namespace

TEST_CASE("Recording: WAV, BWF and FLAC round-trip with every bit depth", "[record]")
{
    struct Case { FileFormat f; BitDepth d; double tol; };
    const Case cases[] = {{FileFormat::Wav, BitDepth::Int24, 3.0 / 8388608.0}, {FileFormat::Wav, BitDepth::Int16, 3.0 / 32768.0},
                          {FileFormat::Wav, BitDepth::Float32, 1e-7},           {FileFormat::Bwf, BitDepth::Int24, 3.0 / 8388608.0},
                          {FileFormat::Flac, BitDepth::Int24, 3.0 / 8388608.0},  {FileFormat::Flac, BitDepth::Int16, 3.0 / 32768.0}};
    for (const auto& c : cases)
    {
        INFO(toString(c.f) << " " << static_cast<int>(c.d));
        pf8test::TempDir dir("rec-roundtrip");
        RecordTap tap;
        Recorder rec(tap, kRate);
        std::string err;
        REQUIRE(rec.start(settings(dir.path(), c.f, c.d), err));
        uint64_t frame = 0;
        pushFrames(tap, frame, kRate * 3 + 123, tap.mask());
        rec.addMarker("Topic change");
        pushFrames(tap, frame, kRate, tap.mask());
        rec.stop();
        const auto session = rec.status().session;

        const auto journal = Journal::load(session / "Metadata" / "Journal.json");
        REQUIRE(journal.has_value());
        CHECK(journal->state == "finalised");
        REQUIRE(journal->tracks.size() == 3); // CH1, CH2, Main
        std::map<std::string, int> trackOf = {{"Host \"One\"", 0}, {"Guest/Two", 1}, {"Main Mix", 8}};
        for (const auto& t : journal->tracks)
        {
            const auto path = session / fs::path(t.file);
            REQUIRE(fs::exists(path));
            CHECK(path.filename().string().find('"') == std::string::npos); // sanitised
            auto reader = openReader(path);
            REQUIRE(reader != nullptr);
            CHECK(static_cast<uint64_t>(reader->lengthInSamples) == frame);
            CHECK(t.samplesWritten == frame);
            const int track = trackOf[t.name];
            juce::AudioBuffer<float> b(static_cast<int>(reader->numChannels), 4800);
            reader->read(&b, 0, 4800, 96000, true, true);
            double worst = 0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < 4800; ++i)
                    worst = std::max(worst, std::abs(static_cast<double>(b.getSample(ch, i)) - signal(track, ch, 96000 + static_cast<uint64_t>(i))));
            CHECK(worst < c.tol);
        }
        if (c.f != FileFormat::Flac)
        {
            const auto mainBytes = readAll(session / "Mix" / (std::string("MainMix") + extension(c.f)));
            CHECK(contains(mainBytes, "cue "));
            CHECK(contains(mainBytes, "Topic change"));
            if (c.f == FileFormat::Bwf)
            {
                CHECK(contains(mainBytes, "bext"));
                CHECK(contains(mainBytes, "Podcast Forge 8"));
                CHECK(contains(mainBytes, "Episode 12"));
                CHECK(contains(mainBytes, "iXML"));
            }
        }
        CHECK(fs::exists(session / "Metadata" / "Markers.json"));
    }
}

TEST_CASE("Recording: WAV promotes to RF64 beyond the threshold", "[record]")
{
    pf8test::TempDir dir("rec-rf64");
    WavWriter w(false, 64 * 1024);
    TrackFileInfo info;
    info.path = dir.path() / "big.wav";
    info.channels = 2;
    info.depth = BitDepth::Int24;
    REQUIRE(w.open(info, std::make_unique<Win32FileSink>()) == SinkError::None);
    std::vector<float> x(2 * 48000);
    for (size_t i = 0; i < x.size(); ++i) x[i] = signal(0, static_cast<int>(i % 2), i / 2);
    REQUIRE(w.write(x.data(), 48000) == SinkError::None);
    REQUIRE(w.finalise({}) == SinkError::None);
    CHECK(w.isRf64());
    const auto bytes = readAll(info.path);
    CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "RF64");
    CHECK(std::string(bytes.begin() + 12, bytes.begin() + 16) == "ds64");
    auto reader = openReader(info.path);
    REQUIRE(reader != nullptr);
    CHECK(reader->lengthInSamples == 48000);
}

TEST_CASE("Recording: crash leaves recoverable files; recovery never changes audio bytes", "[record][recovery]")
{
    pf8test::TempDir dir("rec-crash");
    RecordTap tap;
    auto rec = std::make_unique<Recorder>(tap, kRate);
    std::string err;
    REQUIRE(rec->start(settings(dir.path(), FileFormat::Wav, BitDepth::Int24), err));
    uint64_t frame = 0;
    pushFrames(tap, frame, kRate * 5, tap.mask());
    std::this_thread::sleep_for(std::chrono::milliseconds(2500)); // at least one checkpoint
    const auto session = rec->status().session;
    // "Crash": simulate the process dying — freeze the files as they are, then corrupt the headers
    // the way a crash between checkpoints leaves them (stale sizes) and add a torn partial frame.
    const auto journalBefore = Journal::load(session / "Metadata" / "Journal.json");
    REQUIRE(journalBefore.has_value());
    CHECK(journalBefore->state == "recording");
    rec->stop(); // flushes everything to disk (the bytes are what a crash would also have left, minus the tail)
    rec.reset();
    const auto ch1 = session / "Audio" / "CH01_Host _One_.wav";
    REQUIRE(fs::exists(ch1));
    {
        RecoveryFile f;
        REQUIRE(f.open(ch1));
        const uint8_t zero[4] = {0, 0, 0, 0};
        REQUIRE(f.writeAt(4, zero, 4));
    }
    {
        std::ofstream out(ch1, std::ios::binary | std::ios::app);
        out.put(0x11); // torn partial frame (1 of 3 bytes)
    }
    // Stale header data size too: find 'data' and zero it.
    const auto bytes = readAll(ch1);
    const std::string dataTag = "data";
    const auto dataPos = std::search(bytes.begin(), bytes.end(), dataTag.begin(), dataTag.end()) - bytes.begin();
    {
        RecoveryFile f;
        REQUIRE(f.open(ch1));
        const uint8_t zero[4] = {0, 0, 0, 0};
        REQUIRE(f.writeAt(static_cast<uint64_t>(dataPos) + 4, zero, 4));
    }
    // Mark the session unfinished like a crash would.
    auto j = *Journal::load(session / "Metadata" / "Journal.json");
    j.state = "recording";
    REQUIRE(j.save(session / "Metadata" / "Journal.json"));

    const auto beforeBytes = readAll(ch1);
    const auto audioBefore = std::vector<uint8_t>(beforeBytes.begin() + dataPos + 8, beforeBytes.end());
    const auto unfinished = findUnfinishedSessions(dir.path());
    REQUIRE(unfinished.size() == 1);
    const auto report = recoverSession(unfinished.front());
    CHECK(report.ok());
    const auto after = readAll(ch1);
    CHECK(std::vector<uint8_t>(after.begin() + dataPos + 8, after.end()) == audioBefore); // every audio byte untouched
    auto reader = openReader(ch1);
    REQUIRE(reader != nullptr);
    CHECK(static_cast<uint64_t>(reader->lengthInSamples) == frame); // partial frame excluded
    CHECK(Journal::load(session / "Metadata" / "Journal.json")->state == "recovered");
    CHECK(fs::exists(session / "Metadata" / "Recovery.log"));
    CHECK(findUnfinishedSessions(dir.path()).empty());
}

TEST_CASE("Recording: disk full keeps recording in memory and continues on another drive", "[record][diskfull]")
{
    pf8test::TempDir dir("rec-diskfull"), other("rec-otherdrive");
    Budget budget;
    budget.bytes = 400 * 1024; // the "disk" fills after ~1.4 s of 3 tracks at 24-bit
    budget.freeDir = other.path();
    RecordTap tap;
    Recorder rec(tap, kRate, std::make_unique<FaultyFactory>(budget));
    std::string err;
    REQUIRE(rec.start(settings(dir.path(), FileFormat::Wav, BitDepth::Int24), err));
    uint64_t frame = 0;
    pushFrames(tap, frame, kRate * 4, tap.mask());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto s = rec.status();
    CHECK(s.state == Recorder::State::Recording); // never stops on its own
    CHECK(s.writeError);
    CHECK(s.pendingBytes > 0);
    const auto firstSession = s.session;

    REQUIRE(rec.continueElsewhere(other.path(), err));
    pushFrames(tap, frame, kRate * 2, tap.mask());
    rec.stop();
    CHECK_FALSE(rec.status().writeError);

    // Part 1 files still exist, are valid for what reached the disk; part 2 holds the rest.
    const auto part2 = other.path() / (firstSession.filename().string() + "_part2");
    uint64_t total1 = 0, total2 = 0;
    for (const char* name : {"CH01_Host _One_", "CH02_Guest_Two"})
    {
        auto r1 = openReader(firstSession / "Audio" / (std::string(name) + ".wav"));
        auto r2 = openReader(part2 / "Audio" / (std::string(name) + "_part2.wav"));
        REQUIRE(r1 != nullptr);
        REQUIRE(r2 != nullptr);
        total1 = static_cast<uint64_t>(r1->lengthInSamples);
        total2 = static_cast<uint64_t>(r2->lengthInSamples);
        CHECK(total1 + total2 == frame); // nothing lost
        // The first sample of part 2 continues the signal exactly.
        juce::AudioBuffer<float> b(1, 1);
        r2->read(&b, 0, 1, 0, true, false);
        const int track = std::string(name) == "CH01_Host _One_" ? 0 : 1;
        CHECK(b.getSample(0, 0) == Catch::Approx(signal(track, 0, total1)).margin(1e-6));
    }
    CHECK(total1 > 0);
}

TEST_CASE("Recording: nothing is ever overwritten", "[record][safety]")
{
    pf8test::TempDir dir("rec-overwrite");
    const auto existing = dir.path() / "take.wav";
    { std::ofstream(existing) << "precious"; }
    Win32FileSink sink;
    CHECK(sink.create(existing) == SinkError::Exists);
    CHECK(std::ifstream(existing).get() == 'p');
    CHECK(uniqueFilePath(dir.path(), "take", ".wav").filename() == "take_2.wav");
    const auto a = createSessionFolder(dir.path());
    const auto b = createSessionFolder(dir.path());
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a != *b);
    CHECK(fs::exists(*a / "Audio"));
    CHECK(sanitizeFileName("CON") == "_CON");
    CHECK(sanitizeFileName("a/b:c*?") == "a_b_c__");
    CHECK(sanitizeFileName("  ") == "Track");
}

TEST_CASE("RecordTap pushes all tracks or none, without allocating", "[record][rt]")
{
    RecordTap tap;
    tap.configure((1u << 0) | (1u << 8), kRate, 0.05); // 2400 frames
    tap.setActive(true);
    std::vector<float> mono(256, 0.1f), stereo(512, 0.2f);
    std::array<const float*, kTrackCount> ptr{};
    ptr[0] = mono.data();
    ptr[8] = stereo.data();
    rt::resetCounters();
    int accepted = 0, dropped = 0;
    {
        rt::ScopedRealtime scope;
        for (int i = 0; i < 20; ++i) (tap.push(ptr, 256) ? accepted : dropped)++;
    }
    CHECK(rt::allocationsOnRealtimeThreads() == 0);
    CHECK(accepted > 0);
    CHECK(dropped > 0);
    CHECK(tap.framesDropped() == static_cast<uint64_t>(dropped) * 256);
    CHECK(tap.ring(0)->size() == static_cast<size_t>(accepted) * 256);
    CHECK(tap.ring(8)->size() == static_cast<size_t>(accepted) * 512); // same number of frames
}

TEST_CASE("Recording: isolated tracks are sample-aligned with the Main mix", "[record][harness]")
{
    pf8test::EngineHarness h(48000, 128);
    pf8test::FakeDeviceSpec mic;
    mic.ppm = 120;
    mic.signal = [](int, double t) {
        const double c = std::floor(t) + 0.5, x = (t - c) / 0.0002;
        return static_cast<float>(0.9 * std::exp(-x * x));
    };
    h.addInput(mic);
    pf8test::FakeDeviceSpec hp;
    hp.master = true;
    hp.channels = 2;
    h.addOutput(hp);
    h.route(0, 0, -1, 0);
    h.commitGraph();
    auto& tap = h.engine().recordTap();
    tap.configure((1u << 0) | (1u << 8), 48000, 20.0);
    h.engine().dsp(0).hpfOn = false; // keep the click shape identical
    tap.setActive(true);
    h.run(12.0);
    tap.setActive(false);
    std::vector<float> ch(tap.ring(0)->size()), mainLR(tap.ring(8)->size());
    tap.ring(0)->pop(ch.data(), ch.size());
    tap.ring(8)->pop(mainLR.data(), mainLR.size());
    auto peakPos = [](const float* x, size_t n, size_t stride) {
        size_t best = 0;
        for (size_t i = 0; i < n; ++i)
            if (x[i * stride] > x[best * stride]) best = i;
        return best;
    };
    // Compare the click in seconds 8–9 (after lock).
    const size_t from = 48000 * 8, len = 48000;
    const size_t a = peakPos(ch.data() + from, len, 1);
    const size_t b = peakPos(mainLR.data() + 2 * from, len, 2);
    CHECK(a == b);
}

TEST_CASE("Markers: JSON round trip and exports", "[record][markers]")
{
    MarkerList m;
    m.setSampleRate(48000);
    m.add(48000 * (14 * 60 + 23), "Topic change");
    m.add(48000 * (32 * 60 + 18), "Funny story");
    m.add(48000ull * (51 * 60 + 44), "Sponsor, \"segment\"");
    MarkerList back;
    REQUIRE(back.loadJson(m.toJson()));
    REQUIRE(back.all().size() == 3);
    CHECK(back.all()[0].label == "Topic change");
    CHECK(timecode(back.all()[1].samplePos, 48000, false) == "00:32:18");
    const auto csv = m.exportText(MarkerExport::Csv);
    CHECK(csv.find("00:14:23.000,863.000000,\"Topic change\"") != std::string::npos);
    CHECK(csv.find("\"Sponsor, \"\"segment\"\"\"") != std::string::npos);
    CHECK(m.exportText(MarkerExport::Audacity).find("863.000000\t863.000000\tTopic change") != std::string::npos);
    CHECK(m.exportText(MarkerExport::Reaper).find("M2,\"Funny story\",00:32:18.000") != std::string::npos);
    CHECK(m.exportText(MarkerExport::Audition).find("Topic change\t0:14:23.000") != std::string::npos);
    CHECK(m.rename(2, "Funniest story"));
    CHECK(m.remove(1));
    CHECK(m.all().size() == 2);
}

TEST_CASE("Journal JSON round trip", "[record]")
{
    Journal j;
    j.state = "paused";
    j.prerollSamples = 480000;
    JournalTrack t;
    t.file = "Audio/CH01_Host.wav";
    t.name = "Host";
    t.format = FileFormat::Bwf;
    t.samplesWritten = 123456789;
    t.headerBytes = 1234;
    j.tracks.push_back(t);
    auto back = Journal::fromJson(j.toJson());
    REQUIRE(back.has_value());
    CHECK(back->state == "paused");
    CHECK(back->prerollSamples == 480000);
    CHECK(back->tracks[0].format == FileFormat::Bwf);
    CHECK(back->tracks[0].samplesWritten == 123456789);
    CHECK_FALSE(Journal::fromJson("{").has_value());
}

TEST_CASE("Recovery on a finalised file keeps its header (trailing cue chunks are not audio)", "[record][recovery]")
{
    pf8test::TempDir dir("rec-recover-final");
    RecordTap tap;
    Recorder rec(tap, kRate);
    std::string err;
    REQUIRE(rec.start(settings(dir.path(), FileFormat::Wav, BitDepth::Int24), err));
    uint64_t frame = 0;
    pushFrames(tap, frame, kRate * 2, tap.mask());
    rec.addMarker("one");
    rec.addMarker("two");
    pushFrames(tap, frame, kRate, tap.mask());
    rec.stop();
    const auto main = rec.status().session / "Mix" / "MainMix.wav";
    const auto before = readAll(main);
    const auto r = recoverWavFile(main);
    CHECK(r.ok);
    CHECK(r.frames == frame);
    const bool identical = readAll(main) == before;
    CHECK(identical); // byte-identical: nothing to fix
}
