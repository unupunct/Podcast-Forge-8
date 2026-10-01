#pragma once
// The device-control thread (COM MTA). Owns the DeviceRegistry, channel assignments, every
// WasapiStream and its bridge, and the AudioEngine. All blocking device work (enumerate, open,
// close) happens here — never on the UI or audio threads.
//
// Reconcile loop: registry + assignments → resolution → the set of endpoints channels need →
// open/close only what changed → choose the master → publish a new EngineGraph.
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "devices/DeviceIdentity.h"
#include "devices/DeviceRegistry.h"
#include "engine/AudioEngine.h"
#include "engine/WasapiStream.h"
#include "record/Recorder.h"
#include "routing/TalkbackKey.h"

namespace pf8 {

class SettingsDb;
class HotplugWatcher;

enum class EndpointState : uint8_t { None, Ok, Offline, Disconnected, PossibleMatch, InUse, Failed };
const char* toString(EndpointState s) noexcept;

struct EndpointView
{
    EndpointState state = EndpointState::None;
    std::string assignedName;  // saved identity's name
    std::string endpointId;    // resolved endpoint (or candidate for PossibleMatch)
    std::string detail;        // error text / candidate name
    BridgeStats bridge;
    bool master = false;
    int deviceRate = 0, deviceChannels = 0, periodFrames = 0;
    StreamMode mode = StreamMode::Shared;
};

struct ChannelView
{
    std::string name;
    EndpointView mic;
    int micChannel = -1;
    EndpointView headphones;
    int hpPair = 0;
};

struct ControllerStatus
{
    std::array<EndpointView, kOutputRoles> outputs;
    std::string backend = "WASAPI";
    bool running = false;
    bool internalClock = false;
    std::string masterId, masterName;
    StreamMode masterMode = StreamMode::Shared;
    int sampleRate = 0;
    int blockFrames = 0;
    int masterPeriod = 0;
    int openStreams = 0;
    std::array<ChannelView, kNumChannels> channels;
    EndpointView talkback; // dedicated talkback mic
    int talkbackChannel = -1;
};

class EngineController
{
public:
    struct Settings
    {
        int sampleRate = 48000;
        int blockFrames = 128;
        StreamMode mode = StreamMode::Shared;
    };

    EngineController(Settings settings, SettingsDb* db);
    ~EngineController();
    EngineController(const EngineController&) = delete;
    EngineController& operator=(const EngineController&) = delete;

    // Asynchronous (control thread).
    void start();
    void rescanDevices();
    void assignMic(int channel, std::optional<std::string> endpointId, int micChannel = -1);
    void assignHeadphones(int channel, std::optional<std::string> endpointId, int pair = 0);
    void assignOutput(OutputRole role, std::optional<std::string> endpointId, int pair = 0);
    void assignTalkbackMic(std::optional<std::string> endpointId, int micChannel = -1);
    void applyAssignments(const Assignments& a); // e.g. a confirmed Auto Assign proposal
    void acceptPossibleMatch(int channel, bool mic);
    void setChannelName(int channel, std::string name);

    void post(std::function<void()> job);
    void postDelayed(std::chrono::milliseconds delay, std::function<void()> job);
    void waitIdle();

    const DeviceRegistry& registry() const noexcept { return registry_; }
    AudioEngine& engine() noexcept { return engine_; }

    // Recording (UI thread). Settings other than names/arms come from recordingSettings().
    struct RecordingSettings
    {
        std::filesystem::path projectDir;
        FileFormat format = FileFormat::Wav;
        BitDepth depth = BitDepth::Int24;
        bool recordMain = true;
        bool recordMusic = false;
    };
    RecordingSettings& recordingSettings() noexcept { return recordingSettings_; }
    Recorder::Settings recorderSettings() const; // assembled from assignments + arms + recording settings
    bool startRecording(std::string& error);
    Recorder& recorder() noexcept { return *recorder_; }
    ControllerStatus status() const;
    // Talkback key (UI thread: the TALK button and hotkeys). Drives RoutingParams::talkbackActive.
    void talkbackPress();
    void talkbackRelease();
    TalkbackKey& talkbackKey() noexcept { return talkbackKey_; }
    EngineMeters meters() const noexcept { return engine_.meters(); }
    Assignments assignments() const;
    const Settings& settings() const noexcept { return settings_; }
    SettingsDb* settingsDb() const noexcept { return db_; }

private:
    struct Endpoint;
    class Handler;

    void threadMain();
    void loadAssignments();
    void saveAssignments();
    void reconcile();
    void onHotplug();
    void onStreamEnded(const std::string& key);
    std::unique_ptr<Endpoint> openEndpoint(const std::string& endpointId, Flow flow, bool master, int pairs);

    Settings settings_;
    SettingsDb* db_;
    DeviceRegistry registry_;
    AudioEngine engine_;
    std::unique_ptr<Recorder> recorder_;
    RecordingSettings recordingSettings_;
    TalkbackKey talkbackKey_;
    std::unique_ptr<HotplugWatcher> hotplug_;

    mutable std::mutex stateMutex_; // guards the members below against the UI's status() reads
    Assignments assignments_;
    std::array<ChannelResolution, kNumChannels> resolution_{};
    std::array<Resolution, kOutputRoles> outputResolution_{};
    Resolution talkbackResolution_{};
    std::map<std::string, std::unique_ptr<Endpoint>> endpoints_; // key: "c:" / "r:" + endpoint id
    std::map<std::string, std::string> failures_;                // key → last open error
    std::string masterKey_;
    int masterBurst_ = 0; // engine frames per master callback (control thread)
    uint64_t generation_ = 0;
    std::atomic<int> hotplugPending_{0};

    std::thread thread_;
    std::mutex queueMutex_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> jobs_;
    bool busy_ = false;
    bool quit_ = false;
};

} // namespace pf8
