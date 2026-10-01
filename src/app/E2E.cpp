#include "app/E2E.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <chrono>
#include <optional>
#include <cmath>
#include <thread>

#include "app/CliModes.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "engine/EngineController.h"
#include "record/Journal.h"

namespace pf8::cli {
namespace fs = std::filesystem;
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kSimLevel = 0.1f; // -20 dBFS
constexpr int kTalkbackChannel = 5; // CH6: simulated, muted on every bus, used as the talkback mic
constexpr float kTalkbackHz = 2500.0f;
constexpr float kCartHz = 2000.0f;

// Windows applies the endpoint volume to shared-mode streams: read it so the loopback check expects
// the level the user's mixer settings actually produce. nullopt when unavailable.
std::optional<double> endpointVolumeDb(const std::string& endpointId)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::optional<double> result;
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en))))
    {
        IMMDevice* dev = nullptr;
        const std::wstring wid = juce::String::fromUTF8(endpointId.c_str()).toWideCharPointer();
        if (SUCCEEDED(en->GetDevice(wid.c_str(), &dev)))
        {
            IAudioEndpointVolume* vol = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&vol))))
            {
                float db = 0;
                BOOL mute = FALSE;
                if (SUCCEEDED(vol->GetMasterVolumeLevel(&db))) result = db;
                if (SUCCEEDED(vol->GetMute(&mute)) && mute) result = -180.0;
                vol->Release();
            }
            dev->Release();
        }
        en->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return result;
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

double toDb(double a) { return a > 1e-9 ? 20.0 * std::log10(a) : -180.0; }

// Amplitude of a sine at `hz` in x[0..n) (Goertzel, Hann window, amplitude-corrected).
double toneAmplitude(const float* x, int n, double hz, double rate)
{
    if (n <= 16) return 0.0;
    const double w = 2.0 * kPi * hz / rate, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0, wsum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
        wsum += win;
        const double s = x[i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

struct TrackAudio
{
    std::vector<float> left, right;
    double rate = 0;
    int64_t frames = 0;
    bool ok = false;
};

TrackAudio readTrack(const fs::path& file)
{
    TrackAudio t;
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(mgr.createReaderFor(juce::File(juce::String(file.wstring().c_str()))));
    if (!r) return t;
    t.rate = r->sampleRate;
    t.frames = r->lengthInSamples;
    juce::AudioBuffer<float> b(static_cast<int>(r->numChannels), static_cast<int>(t.frames));
    r->read(&b, 0, static_cast<int>(t.frames), 0, true, r->numChannels > 1);
    t.left.assign(b.getReadPointer(0), b.getReadPointer(0) + t.frames);
    if (b.getNumChannels() > 1) t.right.assign(b.getReadPointer(1), b.getReadPointer(1) + t.frames);
    t.ok = true;
    return t;
}

bool writeToneWav(const fs::path& file, double rate, double hz, double seconds, float level)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(juce::File(juce::String(file.wstring().c_str())));
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
    if (!w) return false;
    const int n = static_cast<int>(rate * seconds);
    juce::AudioBuffer<float> b(2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = level * static_cast<float>(std::sin(2.0 * kPi * hz * i / rate));
        b.setSample(0, i, v);
        b.setSample(1, i, v);
    }
    return w->writeFromAudioSampleBuffer(b, 0, n);
}

struct Report
{
    json::Array checks;
    bool ok = true;
    void check(const std::string& name, bool pass, const std::string& detail, bool required = true)
    {
        json::Object o;
        o["check"] = name;
        o["result"] = pass ? "pass" : (required ? "FAIL" : "warn");
        o["detail"] = detail;
        checks.emplace_back(std::move(o));
        if (!pass && required) ok = false;
        writeStdout(juce::String(pass ? "  PASS  " : (required ? "  FAIL  " : "  WARN  ")) + juce::String::fromUTF8(name.c_str()) + "  " +
                    juce::String::fromUTF8(detail.c_str()) + "\n");
    }
};

std::string fmt(double v, int decimals = 1)
{
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

} // namespace

int runE2E(const juce::StringArray& args)
{
    double seconds = 60.0;
    int loopInput = -1; // device input of the loopback capture (-1 = mix of all)
    const bool trace = args.contains("--trace"); // per-second meter trace on stdout
    fs::path outRoot = paths::appData() / L"e2e";
    for (const auto& a : args)
    {
        if (a.startsWith("--seconds=")) seconds = juce::jlimit(10.0, 3600.0, a.fromFirstOccurrenceOf("=", false, false).getDoubleValue());
        if (a.startsWith("--loop-input=")) loopInput = a.fromFirstOccurrenceOf("=", false, false).getIntValue();
        if (a.startsWith("--out=")) outRoot = fs::path(a.fromFirstOccurrenceOf("=", false, false).unquoted().toWideCharPointer());
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "e2e-%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const fs::path out = outRoot / stamp;
    std::error_code ec;
    fs::create_directories(out, ec);
    writeStdout("Podcast Forge 8 end-to-end test, " + juce::String(seconds, 0) + " s, output " + juce::String(out.wstring().c_str()) + "\n");
    PF8_LOG_INFO("e2e", "e2e.start seconds=%.0f", seconds);

    // No settings database: the user's saved assignments are neither used nor changed.
    EngineController c(EngineController::Settings{}, nullptr);
    c.start();
    c.waitIdle();
    const int rate = c.settings().sampleRate;
    auto& e = c.engine();
    auto& rp = e.routing();

    // ----- devices -----
    std::vector<const DeviceInfo*> mics;
    const DeviceInfo *cableIn = nullptr, *cableOut = nullptr;
    const auto devices = c.registry().devices();
    for (const auto& d : devices)
    {
        if (!d.online()) continue;
        const auto name = juce::String::fromUTF8(d.raw.friendlyName.c_str());
        const bool cable = name.containsIgnoreCase("CABLE") && name.containsIgnoreCase("VB-Audio") && !name.containsIgnoreCase("16ch");
        if (d.raw.flow == Flow::Render && cable && !cableIn) cableIn = &d;
        else if (d.raw.flow == Flow::Capture && cable && !cableOut) cableOut = &d;
        else if (d.raw.flow == Flow::Capture) mics.push_back(&d);
    }
    json::Array devs;
    const bool loop = cableIn && cableOut;
    // Real mics on CH2…CH5 (CH1 and CH6 stay simulated: they carry the reference tones).
    int nextCh = 1;
    std::array<std::string, kNumChannels> role{};
    for (const auto* m : mics)
    {
        if (nextCh > 4) break;
        c.assignMic(nextCh, m->raw.endpointId);
        role[static_cast<size_t>(nextCh)] = "real mic: " + m->raw.friendlyName;
        json::Object o;
        o["channel"] = nextCh + 1;
        o["device"] = m->raw.friendlyName;
        devs.emplace_back(std::move(o));
        ++nextCh;
    }
    if (loop)
    {
        // HP8 → CABLE Input … CABLE Output → CH7: a real playback-to-capture path.
        c.assignHeadphones(7, cableIn->raw.endpointId);
        c.assignMic(6, cableOut->raw.endpointId, loopInput);
        role[6] = "loopback from HP8 via VB-Cable";
    }
    c.waitIdle();
    std::array<float, kNumChannels> simHz{};
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        if (!role[static_cast<size_t>(ch)].empty()) continue;
        if (ch == 7) continue; // CH8: headphones only
        simHz[static_cast<size_t>(ch)] = ch == kTalkbackChannel ? kTalkbackHz : 300.0f + 110.0f * static_cast<float>(ch);
        e.setSimulatedSource(ch, simHz[static_cast<size_t>(ch)], kSimLevel);
        role[static_cast<size_t>(ch)] = "simulated " + fmt(simHz[static_cast<size_t>(ch)], 0) + " Hz";
    }
    // CH6 is the talkback mic: muted, so it is on no bus — only the talkback path carries it.
    rp.channel[kTalkbackChannel].mute = true;
    // CH7 carries HP8 back in (a measurement channel): keep it off every bus, or the loop would put
    // HP8's content (incl. talkback) onto Main. Its isolated track is pre-fader and still recorded.
    if (loop) rp.channel[6].mute = true;
    rp.talkbackSource = kTalkbackChannel;
    rp.talkbackTarget[7] = true;
    // HP8: only CH1's tone, so the loopback has a known reference.
    rp.headphones[7].mode = HpMode::Custom;
    for (int s = 0; s < kSourceCount; ++s) rp.gain[static_cast<size_t>(s)][hpBus(7)].set(0.0f);
    rp.gain[0][hpBus(7)].set(1.0f);

    // Music (generated) with ducking, recorded as its own track; a cart.
    const fs::path musicFile = out / L"e2e-music.wav";
    writeToneWav(musicFile, rate, 150.0, seconds + 30.0, 0.2f);
    e.music().setPlaylist({musicFile});
    e.music().volume().set(1.0f);
    e.ducker().on = true;
    e.music().play(0);
    {
        auto cart = std::make_shared<CartBuffer>();
        const int n = rate;
        cart->frames = n;
        cart->samples.resize(static_cast<size_t>(2 * n));
        for (int i = 0; i < n; ++i) cart->samples[static_cast<size_t>(2 * i)] = cart->samples[static_cast<size_t>(2 * i + 1)] =
                                        0.2f * static_cast<float>(std::sin(2.0 * kPi * kCartHz * i / rate));
        cart->sourcePath = "e2e-generated";
        e.soundboard().setBuffer(0, cart);
    }

    // ----- record -----
    auto& rs = c.recordingSettings();
    rs.projectDir = out / L"Project";
    rs.recordMusic = true;
    if (!e.setPrerollSeconds(5.0)) writeStdout("pre-roll could not be enabled\n");
    sleepMs(6500); // fill the 5 s pre-roll; streams settle
    std::string err;
    Report rep;
    if (!c.startRecording(err))
    {
        rep.check("recording starts", false, err);
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    auto at = [&](double s) { std::this_thread::sleep_until(t0 + std::chrono::milliseconds(static_cast<int64_t>(s * 1000))); };
    float minDuck = 0.0f;
    bool marker = false, cart = false, tbOn = false, tbOff = false, restarted = false;
    // After the level-check window (8 s) and early enough for every device to re-lock (20-25 s).
    const double restartAt = seconds >= 45.0 ? 10.0 : 0.0;
    for (double t = 0.0; t < seconds; t += 0.1)
    {
        at(t);
        minDuck = std::min(minDuck, c.meters().musicDuckDb);
        if (trace && std::fmod(t + 1e-6, 1.0) < 0.05)
        {
            const auto m = c.meters();
            writeStdout("trace t=" + juce::String(t, 1) + " HP8bus " + juce::String(toDb(m.busPeak[static_cast<size_t>(hpBus(7))][0]), 2) + " HP8gr " +
                        juce::String(m.hpProtectGrDb[7], 2) + " CH7pk " + juce::String(toDb(m.peak[6]), 2) + " CH1pk " + juce::String(toDb(m.peak[0]), 2) +
                        " Mainpk " + juce::String(toDb(m.busPeak[0][0]), 2) + "\n");
        }
        if (!marker && t >= 2.0) c.recorder().addMarker("e2e marker"), marker = true;
        if (!cart && t >= 3.0) e.soundboard().play(0), cart = true;
        if (!tbOn && t >= 4.0) rp.talkbackActive = true, tbOn = true;
        if (!tbOff && t >= 6.0) rp.talkbackActive = false, tbOff = true;
        // An audio restart (what the watchdog does) in the middle of the recording: the engine
        // must never run ahead of real time while the master reopens.
        if (restartAt > 0 && !restarted && t >= restartAt) c.restartAudio(), restarted = true;
    }
    const auto status = c.status();
    const auto recStatus = c.recorder().status();
    c.recorder().stop();
    const auto session = recStatus.session;
    writeStdout("recorded into " + juce::String(session.wstring().c_str()) + "\n");

    // ----- validate -----
    const auto journal = Journal::load(session / "Metadata" / "Journal.json");
    rep.check("journal finalised", journal && journal->state == "finalised", journal ? journal->state : "missing");
    rep.check("no recording dropouts", recStatus.droppedFrames == 0, std::to_string(recStatus.droppedFrames) + " frames dropped");
    const uint64_t pre = journal ? journal->prerollSamples : 0;
    rep.check("pre-roll 5 s", pre == static_cast<uint64_t>(rate) * 5, std::to_string(pre) + " frames");
    const double expected = 5.0 + seconds;
    std::map<std::string, TrackAudio> tracks;
    if (journal)
        for (const auto& t : journal->tracks)
        {
            auto a = readTrack(session / paths::fromUtf8(t.file));
            const double secs = a.ok ? static_cast<double>(a.frames) / a.rate : 0.0;
            // A restart pauses the tick while devices reopen (shorter files); running ahead of real
            // time (longer files) is never acceptable.
            const double minSecs = expected - (restarted ? 1.5 : 0.5);
            rep.check("file " + t.file, a.ok && secs > minSecs && secs < expected + 0.25,
                      fmt(secs, 2) + " s (expected " + fmt(expected, 1) + (restarted ? ", audio restarted once" : "") + ")");
            tracks[t.name] = std::move(a);
        }
    const int mid = static_cast<int>((5.0 + 8.0) * rate); // inside the live part, after the talkback press
    const int win = rate / 2;
    // Simulated channels on their isolated tracks at -20 dBFS.
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        if (simHz[static_cast<size_t>(ch)] <= 0) continue;
        const std::string name = c.assignments().ch[static_cast<size_t>(ch)].name.empty() ? "Channel " + std::to_string(ch + 1)
                                                                                           : c.assignments().ch[static_cast<size_t>(ch)].name;
        auto it = tracks.find(name);
        if (it == tracks.end() || !it->second.ok || static_cast<int64_t>(mid + win) > it->second.frames)
        {
            rep.check("CH" + std::to_string(ch + 1) + " simulated tone", false, "track missing");
            continue;
        }
        const double a = toneAmplitude(it->second.left.data() + mid, win, simHz[static_cast<size_t>(ch)], rate);
        rep.check("CH" + std::to_string(ch + 1) + " simulated tone", std::abs(toDb(a) - toDb(kSimLevel)) < 1.0, fmt(toDb(a)) + " dBFS (expected -20.0)");
    }
    // Real mics: something arrives (noise floor at least).
    for (int ch = 1; ch < 5; ++ch)
    {
        if (role[static_cast<size_t>(ch)].rfind("real mic", 0) != 0) continue;
        const std::string name = c.assignments().ch[static_cast<size_t>(ch)].name;
        auto it = tracks.find(name.empty() ? "Channel " + std::to_string(ch + 1) : name);
        float pk = 0;
        if (it != tracks.end())
            for (float v : it->second.left) pk = std::max(pk, std::abs(v));
        rep.check("CH" + std::to_string(ch + 1) + " " + role[static_cast<size_t>(ch)], pk > 1e-6f, "peak " + fmt(toDb(pk)) + " dBFS", false);
    }
    // Main mix: the CH1 tone, the cart, and never the talkback tone.
    if (auto it = tracks.find("Main Mix"); it != tracks.end() && it->second.ok)
    {
        const auto& m = it->second.left;
        const int tb = static_cast<int>((5.0 + 4.3) * rate);
        const double tbMain = toneAmplitude(m.data() + tb, static_cast<int>(1.4 * rate), kTalkbackHz, rate);
        rep.check("talkback absent from Main", toDb(tbMain) < -80.0, fmt(toDb(tbMain)) + " dBFS at " + fmt(kTalkbackHz, 0) + " Hz during the press");
        const double ch1 = toneAmplitude(m.data() + mid, win, simHz[0], rate);
        rep.check("CH1 on Main", std::abs(toDb(ch1) - (-23.01)) < 0.5, fmt(toDb(ch1), 2) + " dBFS (expected -23.01: -20 and the -3 dB centre pan)");
        const int cs = static_cast<int>((5.0 + 3.2) * rate);
        const double cartA = toneAmplitude(m.data() + cs, rate / 2, kCartHz, rate);
        rep.check("cart on Main", toDb(cartA) > -20.0, fmt(toDb(cartA)) + " dBFS at " + fmt(kCartHz, 0) + " Hz");
    }
    else
        rep.check("Main mix track", false, "missing");
    if (auto it = tracks.find("Music"); it != tracks.end() && it->second.ok)
    {
        const double a = toneAmplitude(it->second.left.data() + mid, win, 150.0, rate);
        rep.check("music track", toDb(a) > -40.0, fmt(toDb(a)) + " dBFS (ducked)");
    }
    rep.check("ducking engaged under speech", minDuck < -3.0f, "deepest " + fmt(minDuck) + " dB");
    // Real loop through VB-Cable: CH1's tone comes back on CH7 at unity (±1.5 dB).
    if (loop)
    {
        const std::string name = c.assignments().ch[6].name.empty() ? "Channel 7" : c.assignments().ch[6].name;
        if (auto it = tracks.find(name); it != tracks.end() && it->second.ok)
        {
            const double a = toneAmplitude(it->second.left.data() + mid, win, simHz[0], rate);
            {
                juce::String over = "loopback over time:";
                for (double ts : {1.0, 3.0, 4.5, 7.0, 9.0, 11.0, 14.0})
                {
                    const int at0 = static_cast<int>(ts * rate);
                    if (at0 + win > it->second.frames) break;
                    over << "  t=" << ts - 5.0 << "s " << juce::String(toDb(toneAmplitude(it->second.left.data() + at0, win, simHz[0], rate)), 2);
                }
                writeStdout(over + "\n");
            }
            const auto vIn = endpointVolumeDb(cableIn->raw.endpointId), vOut = endpointVolumeDb(cableOut->raw.endpointId);
            // CH1 (-20) panned centre (-3.01) into HP8, through Windows' volume on both cable ends.
            const double expectDb = toDb(kSimLevel) - 3.01 + vIn.value_or(0.0) + vOut.value_or(0.0);
            rep.check("VB-Cable loopback level", std::abs(toDb(a) - expectDb) < 1.0,
                      fmt(toDb(a), 2) + " dBFS (expected " + fmt(expectDb, 2) + "; Windows volume CABLE Input " + (vIn ? fmt(*vIn, 2) + " dB" : "?") +
                          ", CABLE Output " + (vOut ? fmt(*vOut, 2) + " dB" : "?") + ")");
            // Short windows (~4 Hz wide): while a device is still converging its drift correction
            // (up to 1000 ppm, i.e. +-2.5 Hz at 2.5 kHz) a 1 s window would miss the tone.
            double t = 0.0;
            for (double at = 4.5; at < 5.5; at += 0.25)
                t = std::max(t, toneAmplitude(it->second.left.data() + static_cast<int>((5.0 + at) * rate), rate / 4, kTalkbackHz, rate));
            rep.check("talkback reaches its target headphones", toDb(t) > expectDb - 10.0, fmt(toDb(t)) + " dBFS on HP8 (via loopback)");
        }
    }
    else
        rep.check("VB-Cable loopback", false, "not installed - real playback-to-capture path not tested", false);
    if (journal)
    {
        juce::File mj(juce::String((session / "Metadata" / "Markers.json").wstring().c_str()));
        const auto text = mj.loadFileAsString();
        rep.check("markers", text.contains("Record pressed") && text.contains("e2e marker"), "Record pressed + e2e marker");
    }
    // Streams: sync state and xruns.
    json::Array streams;
    auto streamCheck = [&](const EndpointView& v, const std::string& label) {
        if (v.state == EndpointState::None) return;
        json::Object o;
        o["role"] = label;
        o["device"] = v.assignedName;
        o["state"] = toString(v.state);
        o["sync"] = v.master ? "master" : toString(v.bridge.status);
        o["ppm"] = v.bridge.ppm;
        o["underruns"] = v.bridge.underruns;
        o["overruns"] = v.bridge.overruns;
        streams.emplace_back(std::move(o));
        const bool locked = v.master || v.bridge.status == SyncStatus::Locked || v.bridge.status == SyncStatus::Native;
        rep.check(label + " Windows effects bypassed", v.effectsBypassed, v.effectsBypassed ? "raw stream" : "the driver refused raw mode", false);
        rep.check(label + " sync", v.state == EndpointState::Ok && locked,
                  v.assignedName + ": " + (v.master ? std::string("master") : std::string(toString(v.bridge.status))) + ", " + fmt(v.bridge.ppm) + " ppm, xruns " +
                      std::to_string(v.bridge.underruns + v.bridge.overruns),
                  false);
    };
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        streamCheck(status.channels[static_cast<size_t>(ch)].mic, "CH" + std::to_string(ch + 1) + " mic");
        streamCheck(status.channels[static_cast<size_t>(ch)].headphones, "CH" + std::to_string(ch + 1) + " headphones");
    }
    rep.check("engine load", c.meters().loadPeak < 0.8, "peak " + fmt(c.meters().loadPeak * 100.0) + " %", false);
    rep.check("watchdog", c.watchdogStalls() == 0, std::to_string(c.watchdogStalls()) + " stalls");

    json::Object root;
    root["format"] = "PodcastForge8.E2E";
    root["result"] = rep.ok ? "pass" : "fail";
    root["seconds"] = seconds;
    root["sampleRate"] = rate;
    root["session"] = paths::utf8(session);
    root["loopback"] = loop;
    root["devices"] = std::move(devs);
    root["streams"] = std::move(streams);
    root["checks"] = std::move(rep.checks);
    root["diagnostics"] = json::parse(c.diagnosticsReport()).value_or(json::Value());
    const fs::path reportFile = out / L"e2e-report.json";
    juce::File(juce::String(reportFile.wstring().c_str())).replaceWithText(juce::String::fromUTF8(json::serialize(json::Value(std::move(root)), true).c_str()));
    writeStdout(juce::String(rep.ok ? "E2E: PASS" : "E2E: FAIL") + "  report " + juce::String(reportFile.wstring().c_str()) + "\n");
    PF8_LOG_INFO("e2e", "e2e.end result=%s", rep.ok ? "pass" : "fail");
    return rep.ok ? 0 : 1;
}

} // namespace pf8::cli
