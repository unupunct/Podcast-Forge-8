# Stage 1 — Shell, Device Enumeration, Audio Backend

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Release x64 app that starts, logs, enumerates every Windows audio endpoint with stable identity, opens a WASAPI stream by endpoint ID and drives a (silent) master tick, and shows backend / rate / buffer in a top bar.

**Architecture:** see master plan. Stage 1 creates `core`, `devices`, `engine` (backend layer only), `app`, `ui` (minimal) and the test executable.

**Tech Stack:** JUCE 8.0.15 (third_party/JUCE), Catch2 v3.16.0 (third_party/Catch2), SQLite 3.53.4 (third_party/sqlite), MSVC 14.44, CMake/Ninja from VS BuildTools.

**Spec:** `docs/superpowers/specs/2026-09-29-podcast-forge-8-design.md`, `docs/ARCHITECTURE.md`, `docs/AUDIO_ENGINE.md`, `docs/DEVICE_MANAGEMENT.md`.

## Global Constraints
(Master plan "Global Constraints" apply verbatim.)

## File map

```
CMakeLists.txt                      root: options, third_party, subdirs, app, tests
CMakePresets.json                   "release" (Ninja, Release, x64), "debug"
cmake/PF8Warnings.cmake             pf8_set_warnings(target): /W4 /permissive- /utf-8 /Zc:__cplusplus
tools/env.ps1                       enter VS x64 dev env; add bundled cmake+ninja to PATH (gitignored dir → committed via !tools/env.ps1)
third_party/CMakeLists.txt          sqlite3 static lib; JUCE add_subdirectory; Catch2 add_subdirectory
src/core/CMakeLists.txt             pf8_core
src/core/SpscRing.h                 single-producer/single-consumer ring of T (power-of-two)
src/core/MpscQueue.h                bounded multi-producer/single-consumer queue (Vyukov)
src/core/AtomicParam.h              AtomicParam<float>, relaxed load/store
src/core/SeqLock.h                  SeqLockSnapshot<T> (trivially copyable T)
src/core/RealtimeGuard.h/.cpp       thread-local RT flag + allocation counter (test builds hook operator new)
src/core/Log.h/.cpp                 Log front-end (RT-safe push), LogWriter thread, JSON lines, rotation
src/core/Paths.h/.cpp               %LOCALAPPDATA%\PodcastForge8\{Logs,Diagnostics,verify}, Documents\PodcastForge8\Projects
src/devices/CMakeLists.txt          pf8_devices
src/devices/DeviceInfo.h            EndpointRaw, DeviceInfo, DeviceKind, DeviceState, classify()
src/devices/DeviceInfo.cpp
src/devices/UsbId.h/.cpp            parseUsbInterfacePath() → vid,pid,instance,hasSerial
src/devices/DeviceRegistry.h/.cpp   rebuild(snapshot) → diff (added/removed/changed); thread-safe copy-out
src/devices/WinEndpointEnumerator.h/.cpp  MMDevice + IDeviceTopology + SetupAPI → vector<EndpointRaw>
src/engine/CMakeLists.txt           pf8_engine
src/engine/StreamTypes.h            StreamConfig, StreamMode, StreamStatus, StreamStats
src/engine/WasapiStream.h/.cpp      event-driven WASAPI capture/render stream opened by endpoint ID
src/engine/InternalClock.h/.cpp     MMCSS waitable-timer tick source
src/engine/MasterClock.h/.cpp       selectMaster() pure function
src/engine/AudioEngine.h/.cpp       owns streams, master selection, tick(), status snapshot
src/ui/CMakeLists.txt               pf8_ui
src/ui/LookAndFeel.h/.cpp           dark console palette
src/ui/TopBar.h/.cpp                project, backend, mode, rate, buffer, CPU, disk, devices, rec status
src/ui/DeviceListView.h/.cpp        TableListBox of DeviceInfo
src/ui/MainWindow.h/.cpp
src/app/CMakeLists.txt              PodcastForge8 (juce_add_gui_app)
src/app/Main.cpp                    JUCEApplication; CLI dispatch
src/app/CliModes.h/.cpp             --list-devices
src/app/app.manifest                asInvoker, PerMonitorV2 DPI
tests/CMakeLists.txt                pf8_tests (Catch2WithMain), catch_discover_tests
tests/unit/test_spsc.cpp  test_mpsc.cpp  test_seqlock.cpp  test_realtime_guard.cpp
tests/unit/test_log.cpp  test_usbid.cpp  test_classify.cpp  test_registry.cpp
tests/unit/test_master_clock.cpp  test_internal_clock.cpp
tests/live/test_live_devices.cpp    [live] tag: real enumerator + open default render endpoint 1 s
docs/CHANGELOG.md
README.md
```

---

### Task 1: Build skeleton that compiles and runs one test

**Files:** root `CMakeLists.txt`, `CMakePresets.json`, `cmake/PF8Warnings.cmake`, `tools/env.ps1`, `third_party/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/core/SpscRing.h` (empty namespace for now), `tests/CMakeLists.txt`, `tests/unit/test_spsc.cpp`, `.gitignore` (add `!tools/env.ps1`).

**Produces:** targets `pf8_core` (INTERFACE→STATIC once .cpp exist), `sqlite3`, `pf8_tests`; presets `release`, `debug`; `tools/env.ps1`.

- [ ] Step 1: `tools/env.ps1` — locate BuildTools via vswhere, run `VC\Auxiliary\Build\vcvars64.bat` and import its environment into the PowerShell session; prepend `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` and `...\CMake\Ninja` to PATH.

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] } }
$env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
```

- [ ] Step 2: root `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.25)`, `project(PodcastForge8 VERSION 0.1.0 LANGUAGES C CXX)`, C++20, `CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>` (static CRT → portable exe), options `PF8_ASIO_SDK_DIR` (PATH, empty) and `PF8_BUILD_TESTS` (ON), `add_subdirectory(third_party)`, then src modules, then tests.
- [ ] Step 3: `CMakePresets.json` with configure presets `release`/`debug` (generator Ninja, binaryDir `build/${presetName}`), build and test presets of the same names.
- [ ] Step 4: `third_party/CMakeLists.txt`: `add_library(sqlite3 STATIC sqlite/sqlite3.c)` with `SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1`; `add_subdirectory(JUCE)`; `add_subdirectory(Catch2)` when tests are on.
- [ ] Step 5: failing test `tests/unit/test_spsc.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/SpscRing.h"
TEST_CASE("SpscRing capacity rounds up to power of two", "[core]") {
    pf8::SpscRing<float> r(100);
    REQUIRE(r.capacity() == 128);
}
```
- [ ] Step 6: configure + build → expected compile failure (`SpscRing` undefined). Commit skeleton after Task 2 makes it pass.

### Task 2: core lock-free primitives + RealtimeGuard

**Produces:**
```cpp
namespace pf8 {
template <typename T> class SpscRing {           // T trivially copyable
public:
  explicit SpscRing(size_t minCapacity);         // allocates; capacity = next pow2
  size_t capacity() const noexcept;
  size_t size() const noexcept;                  // readable count (consumer view)
  size_t freeSpace() const noexcept;             // producer view
  size_t push(const T* src, size_t n) noexcept;  // returns written (≤ n), never blocks
  size_t pop(T* dst, size_t n) noexcept;         // returns read (≤ n)
  size_t discard(size_t n) noexcept;             // consumer drops n
  void   reset() noexcept;                       // only when both sides are stopped
};
template <typename T> class MpscQueue {          // bounded, Vyukov sequence cells
public:
  explicit MpscQueue(size_t minCapacity);
  bool tryPush(T&& v) noexcept;  bool tryPush(const T& v) noexcept;
  bool tryPop(T& out) noexcept;                  // single consumer
};
class AtomicParam { public: explicit AtomicParam(float v=0.f) noexcept;
  float get() const noexcept; void set(float v) noexcept; };
template <typename T> class SeqLockSnapshot {    // single writer, many readers
public:
  void write(const T& v) noexcept;
  bool tryRead(T& out) const noexcept;           // false if torn (retry next frame)
  T    read() const noexcept;                    // spins up to 64 tries, returns last good
};
namespace rt {
  struct ScopedRealtime { ScopedRealtime() noexcept; ~ScopedRealtime() noexcept; };
  bool isRealtimeThread() noexcept;
  uint64_t allocationsOnRealtimeThreads() noexcept;  // counted only when PF8_RT_GUARD=1
  void resetCounters() noexcept;
}
}
```
Memory orders: SPSC head/tail with acquire/release, cache-line separated (`alignas(64)`). RealtimeGuard: `operator new`/`delete` replacements live in `tests/support/rt_new.cpp` compiled only into `pf8_tests` (so the app keeps the CRT allocator); they bump an atomic counter when `isRealtimeThread()`.

- [ ] Step 1: tests (`test_spsc.cpp`, `test_mpsc.cpp`, `test_seqlock.cpp`, `test_realtime_guard.cpp`):
  - SPSC: push/pop wrap-around keeps order over 10 000 random-sized ops; `push` on a full ring returns 0; a two-thread stress test (producer writes an incrementing counter 5 M values, consumer verifies sequence) passes.
  - MPSC: 4 producers × 250 000 tagged values, consumer sees each producer's values in order and total = 1 M; `tryPush` on a full queue returns false.
  - SeqLock: writer thread writes `{a=i, b=-i}` 1 M times; readers never observe `a != -b` from a successful `tryRead`.
  - RT guard: inside `ScopedRealtime`, `new int` bumps the counter by 1; outside it doesn't.
- [ ] Step 2: run → fail. Step 3: implement headers + `RealtimeGuard.cpp` + `tests/support/rt_new.cpp`. Step 4: `ctest --preset release` → pass.
- [ ] Step 5: commit `feat(core): lock-free rings, queue, params, seqlock, RT guard`.

### Task 3: Structured logger with rotation

**Produces:**
```cpp
namespace pf8::log {
enum class Level : uint8_t { Debug, Info, Warn, Error };
struct Config { std::filesystem::path dir; uint64_t maxFileBytes = 10ull<<20; int maxFiles = 10; Level minLevel = Level::Info; };
void start(const Config&);          // spawns writer thread, opens podcastforge8-YYYYMMDD-N.jsonl
void stop();                        // drains, closes
// RT-safe: copies into a fixed 512-byte event, drops (and counts) when the queue is full
void write(Level, const char* category, const char* fmt, ...) noexcept;
uint64_t droppedEvents() noexcept;
}
#define PF8_LOG_INFO(cat, ...)  ::pf8::log::write(::pf8::log::Level::Info,  cat, __VA_ARGS__)
#define PF8_LOG_WARN(cat, ...)  ::pf8::log::write(::pf8::log::Level::Warn,  cat, __VA_ARGS__)
#define PF8_LOG_ERROR(cat, ...) ::pf8::log::write(::pf8::log::Level::Error, cat, __VA_ARGS__)
```
Line format: `{"ts":"2026-09-29T10:11:12.345Z","lvl":"warn","cat":"device","tid":1234,"msg":"..."}`. The message is JSON-escaped by the writer thread (not the RT thread). Rotation: when the current file would exceed `maxFileBytes`, open `-N+1`; delete the oldest `podcastforge8-*.jsonl` beyond `maxFiles` (the only deletion the app ever performs, restricted to its own log pattern in its own Logs dir).

- [ ] Step 1: tests: (a) 3 lines → file has 3 valid JSON lines with the fields above; (b) quotes/backslashes/control chars/UTF-8 escape correctly; (c) `maxFileBytes=2048, maxFiles=3`, write 200 lines → exactly 3 files remain, the newest contains the last line; (d) `write` under `ScopedRealtime` allocates 0 times; (e) a file not matching the log pattern in the dir is never deleted.
- [ ] Step 2–4: fail → implement `Log.cpp` (MpscQueue<Event> 4096, condition-free writer that sleeps 20 ms when empty) → pass.
- [ ] Step 5: commit `feat(core): structured JSON logger with rotation`.

### Task 4: Device model, USB id parsing, classification, registry diff

**Produces:**
```cpp
namespace pf8 {
enum class Flow : uint8_t { Capture, Render };
enum class DeviceState : uint8_t { Active, Disabled, Unplugged, NotPresent };
enum class DeviceKind : uint8_t { UsbMicrophone, UsbHeadset, UsbInterface, UsbHeadphones, BuiltIn, Hdmi, Virtual, Bluetooth, Other };
enum class FormFactor : uint8_t { Unknown, Speakers, LineLevel, Headphones, Microphone, Headset, Handset, Digital, Spdif, Hdmi, Other };
struct UsbId { uint16_t vid=0, pid=0; std::string instance; bool hasSerial=false; };
std::optional<UsbId> parseUsbInterfacePath(std::string_view path);  // "\\?\usb#vid_046d&pid_0a38&mi_00#7&2a1b&0&0000#{...}"
struct EndpointRaw {            // exactly what the OS enumerator returns
  std::string endpointId, friendlyName, deviceDesc, manufacturer, enumerator, containerId, parentInterfacePath;
  Flow flow; DeviceState state; FormFactor formFactor;
  int mixRate=0, mixChannels=0; std::vector<int> exclusiveRates;
};
struct DeviceInfo {
  EndpointRaw raw; DeviceKind kind; std::optional<UsbId> usb; bool pairedInContainer=false;
  std::string displayName() const;   // friendlyName, plus " [OFFLINE]" when not Active
};
std::vector<DeviceInfo> classify(const std::vector<EndpointRaw>&);
struct RegistryDiff { std::vector<std::string> added, removed, changed; bool empty() const; };
class DeviceRegistry {
public:
  RegistryDiff rebuild(std::vector<EndpointRaw> snapshot);   // control thread
  std::vector<DeviceInfo> devices() const;                   // copy, mutex-protected (never on RT)
  std::optional<DeviceInfo> find(const std::string& endpointId) const;
};
}
```
Classification rules (in order): enumerator `USB` → if the container has both capture and render → `UsbHeadset` (for both endpoints); capture with formFactor Microphone/Unknown and no render sibling → `UsbMicrophone`; render only with Headphones/Headset → `UsbHeadphones`; ≥ 3 channels or both flows with LineLevel → `UsbInterface`. `BTHENUM`/`BTHLEDevice` → `Bluetooth`. `HDAUDIO` with formFactor Hdmi/Digital or a name containing HDMI/DisplayPort/"Display Audio" → `Hdmi`; other `HDAUDIO`/`INTELAUDIO` → `BuiltIn`. `ROOT`/`SWD` or a known virtual manufacturer (VB-Audio, Voicemeeter, "Virtual") → `Virtual`. Else `Other`.

- [ ] Step 1: tests:
  - `parseUsbInterfacePath` on `\\?\usb#vid_046d&pid_0a38&mi_00#7&2a1b3c&0&0000#{6994ad04-93ef-11d0-a3cc-00a0c9223196}` → vid 0x046D, pid 0x0A38, instance `7&2a1b3c&0&0000`, hasSerial false; with instance `A1B2C3D4E5` → hasSerial true; a non-USB path → nullopt; upper-case `USB#VID_…` also parsed.
  - `classify`: a fixture with Blue Yeti (USB capture), HyperX Cloud (USB capture+render same container), Realtek speakers (HDAUDIO render), NVIDIA HDMI (HDAUDIO, name "NVIDIA High Definition Audio"), VB-Cable (ROOT, manufacturer "VB-Audio Software") → Yeti=UsbMicrophone, both HyperX = UsbHeadset with pairedInContainer, Realtek=BuiltIn, NVIDIA=Hdmi, VB=Virtual.
  - `DeviceRegistry::rebuild`: first build → all added; second with one removed and one state change → diff lists exactly those; unchanged snapshot → empty diff.
- [ ] Step 2–4: fail → implement → pass. Step 5: commit `feat(devices): device model, USB id parsing, classification, registry`.

### Task 5: Windows endpoint enumerator (live)

**Produces:** `std::vector<EndpointRaw> enumerateEndpoints();` in `WinEndpointEnumerator.h` (COM initialised by the caller's thread; the function asserts `CoInitializeEx` succeeded or was already done).

Implementation: `IMMDeviceEnumerator::EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL)`; for each: `GetId`, `GetState`, flow via `IMMEndpoint::GetDataFlow`; property store: `PKEY_Device_FriendlyName`, `PKEY_Device_DeviceDesc`, `PKEY_AudioEndpoint_FormFactor`, `PKEY_Device_EnumeratorName`, `PKEY_Device_ContainerId`, `PKEY_AudioEngine_DeviceFormat` (WAVEFORMATEX blob → rate/channels). Parent interface path via `IDeviceTopology` → `GetConnector(0)` → `GetConnectedTo` → `IPart::GetTopologyObject` → `IDeviceTopology::GetDeviceId`. Manufacturer via `SetupDiCreateDeviceInfoList` + `SetupDiOpenDeviceInterfaceW(path)` + `SetupDiGetDeviceRegistryPropertyW(SPDRP_MFG)`. Exclusive rates: only for Active endpoints, `Activate(IAudioClient)` + `IsFormatSupported(EXCLUSIVE, WAVEFORMATEXTENSIBLE float32 / int24-in-32 / int16 at 44100/48000/88200/96000)`. Every COM failure → leave the field empty; never throw.

- [ ] Step 1: `[live]` test: enumeration returns ≥ 1 endpoint; every endpoint has a non-empty id starting with `{0.0.`; active endpoints have mixRate > 0; the endpoint with a USB parent path parses with `parseUsbInterfacePath`. The test is skipped (`SKIP`) when zero endpoints exist.
- [ ] Step 2–4: implement → `pf8_tests "[live]"` passes on this machine (USB Audio Device, Realtek, NVIDIA, VB-Cable present).
- [ ] Step 5: commit `feat(devices): WASAPI/MMDevice endpoint enumerator`.

### Task 6: WasapiStream, InternalClock, MasterClock selection, AudioEngine skeleton

**Produces:**
```cpp
namespace pf8 {
enum class StreamMode : uint8_t { Shared, SharedLowLatency, Exclusive };
struct StreamConfig { std::string endpointId; Flow flow; StreamMode mode=StreamMode::Shared; int requestedRate=48000; int requestedFrames=128; };
enum class StreamStatus : uint8_t { Closed, Running, Failed, Disconnected, InUseExclusive };
struct StreamStats { std::atomic<uint64_t> callbacks{0}, glitches{0}, framesProcessed{0}; std::atomic<int64_t> lastQpc{0}; };
class StreamCallback { public: virtual ~StreamCallback()=default;
  // capture: data = device frames, interleaved float; render: fill data (interleaved float)
  virtual void onStreamBlock(float* interleaved, int frames, int channels, int64_t qpc, uint64_t devicePosition) noexcept = 0; };
class WasapiStream {
public:
  WasapiStream(StreamConfig, StreamCallback*);
  ~WasapiStream();                               // stops
  bool open(std::string& error);                 // resolves by endpoint ID; picks format
  bool start(); void stop();
  StreamStatus status() const noexcept;
  int  sampleRate() const noexcept; int channels() const noexcept;
  int  periodFrames() const noexcept;            // granted
  StreamMode grantedMode() const noexcept;       // may be Shared if LowLatency unavailable
  const StreamStats& stats() const noexcept;
};
class InternalClock { public: InternalClock(TickClient*, int sampleRate, int blockFrames);
  void start(); void stop(); uint64_t ticks() const noexcept; };
struct MasterCandidate { std::string endpointId; Flow flow; bool online; bool userPreferred; bool isHeadphone; int order; };
std::optional<std::string> selectMaster(const std::vector<MasterCandidate>&);  // nullopt → InternalClock
class AudioEngine : public TickClient {
public:
  struct Status { std::string backend, master; StreamMode mode; int sampleRate, blockFrames, grantedPeriod; bool internalClock; double load; uint64_t ticks; };
  explicit AudioEngine(int sampleRate=48000, int blockFrames=128);
  bool startWithMaster(const std::optional<StreamConfig>& master);   // nullopt → InternalClock
  void stop();
  Status status() const;                  // from SeqLockSnapshot
  void tick(int numFrames) noexcept override;
};
}
```
WasapiStream details: own thread with `AvSetMmThreadCharacteristicsW(L"Pro Audio")`; event-driven (`AUDCLNT_STREAMFLAGS_EVENTCALLBACK`). Shared: mix format (float32), `IAudioClient3::InitializeSharedAudioStream` with the min period for SharedLowLatency, else `Initialize` with a 10 ms (or requested) buffer. Exclusive: try float32 → int24-in-32 → int16 at the requested rate, period = `requestedFrames` aligned per `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED` retry; integer formats are converted to/from float in the stream thread using pre-allocated scratch. `AUDCLNT_E_DEVICE_INVALIDATED` → status `Disconnected`, thread exits cleanly. `AUDCLNT_E_DEVICE_IN_USE` → `InUseExclusive`. Timestamps via `IAudioClock::GetPosition(&pos, &qpc)` on capture `GetBuffer(... &devPos, &qpcPos)`.
Engine: if the master is a render stream, its callback calls `tick(frames)` and writes the Main bus (silence in Stage 1). Load = QPC tick duration / block period, EWMA.

- [ ] Step 1: tests:
  - `selectMaster`: user-preferred online wins; else the first online headphone by `order`; else the first online input; all offline → nullopt; a user-preferred but offline candidate is skipped.
  - `InternalClock` at 48 kHz / 480 frames for 1 s → 100 ticks ±5.
  - `[live]` open the default render endpoint Shared, run 1 s through AudioEngine → ticks ≥ 50, status Running, grantedPeriod > 0, zero glitches reported; stop → Closed.
- [ ] Step 2–4: fail → implement → pass (`ctest` + `pf8_tests "[live]"`).
- [ ] Step 5: commit `feat(engine): WASAPI stream by endpoint id, internal clock, master selection`.

### Task 7: App shell, top bar, device list, --list-devices

**Produces:** `PodcastForge8.exe`: on start → `log::start` → enumerate (control thread with COM) → registry → engine starts on the default render endpoint (shared) or the InternalClock → MainWindow (1600×900 min 1280×720, resizable) with TopBar + DeviceListView. `--list-devices` → attaches to the parent console (`AttachConsole(ATTACH_PARENT_PROCESS)`), prints a JSON array of DeviceInfo, exits 0.

TopBar fields: `PROJECT —`, `BACKEND WASAPI Shared` (granted mode), `48000 Hz`, `128 smp (10.0 ms granted)`, `CPU x %`, `LOAD x %`, `DISK xxx GB free`, `DEVICES n online`, `● STOPPED`. Refreshed by a 10 Hz timer from `AudioEngine::status()`.
DeviceListView columns: Name, Manufacturer, Type, In/Out, Rates, Channels, Device ID, Status.

- [ ] Step 1: build Release; run `PodcastForge8.exe --list-devices` from PowerShell → valid JSON (parse with `ConvertFrom-Json`) listing the USB Audio Device, Realtek, NVIDIA and VB-Cable endpoints with kinds.
- [ ] Step 2: start the app normally, confirm via the log file (not screenshots) that `engine.start` logged `master=<id> mode=Shared rate=48000 period=...`, then close it through `WM_CLOSE` and confirm `app.exit clean` logged.
- [ ] Step 3: `README.md` (AGPLv3, build steps, status) + `docs/CHANGELOG.md` Stage 1 entry.
- [ ] Step 4: stage gate (configure, build, ctest, `[live]`), commit `feat(app): Stage 1 shell with device list and top bar`.
