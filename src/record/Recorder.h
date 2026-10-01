#pragma once
// Multitrack recorder (RECORDING.md). Control-thread API; a worker thread drains the engine's
// RecordTap into track writers, checkpoints headers + journal every 2 s, watches the disk, and
// handles write errors without ever stopping the recording on its own or deleting audio.
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "record/DiskGuard.h"
#include "record/FileSink.h"
#include "record/Journal.h"
#include "record/Markers.h"
#include "record/RecordTap.h"
#include "record/TrackWriter.h"
#include "record/WavWriter.h"

namespace pf8 {

class Recorder
{
public:
    enum class State : uint8_t { Idle, Recording, Paused };

    struct Settings
    {
        std::filesystem::path projectDir;
        FileFormat format = FileFormat::Wav;
        BitDepth depth = BitDepth::Int24;
        std::array<bool, 8> armed{true, true, true, true, true, true, true, true};
        std::array<std::string, 8> names{};
        bool recordMain = true;
        bool recordMusic = false; // track 11: the (ducked) music channel
        std::string description;
        uint64_t rf64Threshold = WavWriter::kDefaultRf64Threshold; // test hook
        double ringSeconds = 2.0;
        uint64_t prerollFrames = 0; // written before the live audio (Stage 11)
    };

    struct Status
    {
        State state = State::Idle;
        std::filesystem::path session;
        uint64_t frames = 0;           // recorded length (incl. pre-roll)
        double seconds = 0.0;
        uint64_t bytesOnDisk = 0;
        double writeBytesPerSecond = 0.0;
        DiskEstimate disk;
        uint64_t droppedFrames = 0;    // blocks the tick couldn't hand over (disk too slow / full)
        uint64_t dropEvents = 0;
        bool writeError = false;
        std::string writeErrorText;
        double writeErrorSeconds = 0.0;
        uint64_t pendingBytes = 0;     // audio held in memory while the disk refuses writes
        int markerCount = 0;
        int tracks = 0;
    };

    Recorder(RecordTap& tap, int sampleRate, std::unique_ptr<SinkFactory> factory = std::make_unique<Win32SinkFactory>());
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Estimate for the pre-record disk check (bytes/s for the given settings).
    double bytesPerSecond(const Settings& s) const;

    bool start(const Settings& settings, std::string& error);
    void pause();
    void resume();
    void stop(); // drains, finalises every file, writes the final journal and markers

    // During a write failure: finalise the current files to what reached the disk and continue in
    // new files under `dir`. Nothing is deleted.
    bool continueElsewhere(const std::filesystem::path& dir, std::string& error);

    int addMarker(const std::string& label = {});
    MarkerList& markers() noexcept { return markers_; }
    std::filesystem::path metadataDir() const;

    Status status() const;
    State state() const noexcept { return state_.load(); }

    // Pre-roll hand-off: frames to write before live audio, per track (Stage 11 fills these).
    void setPreroll(std::array<std::vector<float>, kTrackCount> data, uint64_t frames);

private:
    struct Track
    {
        int id = 0;
        int channels = 1;
        std::unique_ptr<TrackWriter> writer;
        JournalTrack journal;
        std::filesystem::path relativeTo;
    };

    void workerMain();
    void drainOnce(bool final);
    void checkpoint();
    void retryErrors();
    bool openTrack(Track& t, const std::filesystem::path& file, const std::string& name, std::string& error);
    void performContinue();
    void noteError(SinkError e, const std::string& what);

    RecordTap& tap_;
    int rate_;
    std::unique_ptr<SinkFactory> factory_;
    Settings settings_;
    std::atomic<State> state_{State::Idle};

    mutable std::mutex mutex_; // guards everything below
    std::vector<Track> tracks_;
    Journal journal_;
    std::filesystem::path session_, journalPath_;
    std::vector<float> drainBuffer_;
    bool writeError_ = false;
    std::string writeErrorText_;
    std::chrono::steady_clock::time_point errorSince_{};
    std::optional<std::filesystem::path> continueRequest_;
    std::string continueError_;
    bool continueDone_ = false;
    double writeRate_ = 0.0;
    uint64_t lastBytes_ = 0;
    std::chrono::steady_clock::time_point lastRateTime_{};
    DiskEstimate disk_;
    std::array<std::vector<float>, kTrackCount> preroll_;
    uint64_t prerollFrames_ = 0;

    MarkerList markers_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::condition_variable cv_;
};

const char* toString(Recorder::State s) noexcept;

} // namespace pf8
