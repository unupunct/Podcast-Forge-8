#include "project/ProjectState.h"
#include "core/Paths.h"

#include <cmath>
#include <system_error>

#include "devices/DeviceIdentity.h"
#include "engine/EngineController.h"

namespace pf8::project {
namespace {

// ----- the two visitors -----

struct Writer
{
    json::Object& o;
    void num(const char* k, AtomicParam& p) { o[k] = static_cast<double>(p.get()); }
    void flag(const char* k, std::atomic<bool>& b) { o[k] = b.load(); }
    template <class E> void en(const char* k, std::atomic<E>& e, int /*count*/) { o[k] = static_cast<int>(e.load()); }
    void integer(const char* k, std::atomic<int>& i, int, int) { o[k] = i.load(); }
    template <class F> void sub(const char* k, F f)
    {
        json::Object child;
        Writer w{child};
        f(w);
        o[k] = json::Value(std::move(child));
    }
};

struct Reader
{
    const json::Value& v;
    void num(const char* k, AtomicParam& p)
    {
        const auto& x = v[k];
        if (x.isNumber() && std::isfinite(x.asNumber())) p.set(static_cast<float>(x.asNumber()));
    }
    void flag(const char* k, std::atomic<bool>& b)
    {
        if (v[k].isBool()) b = v[k].asBool();
    }
    template <class E> void en(const char* k, std::atomic<E>& e, int count)
    {
        const auto& x = v[k];
        if (x.isNumber() && x.asInt() >= 0 && x.asInt() < count) e = static_cast<E>(x.asInt());
    }
    void integer(const char* k, std::atomic<int>& i, int lo, int hi)
    {
        const auto& x = v[k];
        if (x.isNumber() && x.asInt() >= lo && x.asInt() <= hi) i = x.asInt();
    }
    template <class F> void sub(const char* k, F f)
    {
        const auto& child = v[k];
        if (!child.isObject()) return;
        Reader r{child};
        f(r);
    }
};

// ----- field lists (one per group, shared by both directions) -----

template <class V> void visitDsp(V& v, ChannelDspParams& p)
{
    v.num("inputTrimDb", p.inputTrimDb);
    v.flag("polarityInvert", p.polarityInvert);
    v.flag("hpfOn", p.hpfOn);
    v.num("hpfHz", p.hpfHz);
    v.flag("hpf24dB", p.hpf24dB);
    v.flag("gateOn", p.gateOn);
    v.num("gateThresholdDb", p.gateThresholdDb);
    v.num("gateRangeDb", p.gateRangeDb);
    v.num("gateAttackMs", p.gateAttackMs);
    v.num("gateHoldMs", p.gateHoldMs);
    v.num("gateReleaseMs", p.gateReleaseMs);
    v.flag("eqOn", p.eqOn);
    static const char* bands[4] = {"eq1", "eq2", "eq3", "eq4"};
    for (size_t i = 0; i < 4; ++i)
        v.sub(bands[i], [&](auto& b) {
            b.flag("on", p.eq[i].on);
            b.en("type", p.eq[i].type, 3);
            b.num("freq", p.eq[i].freq);
            b.num("gainDb", p.eq[i].gainDb);
            b.num("q", p.eq[i].q);
        });
    v.flag("deesserOn", p.deesserOn);
    v.num("deessFreq", p.deessFreq);
    v.num("deessThresholdDb", p.deessThresholdDb);
    v.num("deessAmountDb", p.deessAmountDb);
    v.flag("compOn", p.compOn);
    v.num("compThresholdDb", p.compThresholdDb);
    v.num("compRatio", p.compRatio);
    v.num("compAttackMs", p.compAttackMs);
    v.num("compReleaseMs", p.compReleaseMs);
    v.num("compKneeDb", p.compKneeDb);
    v.num("compMakeupDb", p.compMakeupDb);
    v.flag("compAutoMakeup", p.compAutoMakeup);
    v.en("compDetector", p.compDetector, 2);
    v.flag("limiterOn", p.limiterOn);
    v.num("limiterCeilingDb", p.limiterCeilingDb);
    v.num("reverbSend", p.reverbSend);
}

template <class V> void visitMasterDsp(V& v, MasterDspParams& p)
{
    v.flag("limiterOn", p.limiterOn);
    v.num("limiterCeilingDb", p.limiterCeilingDb);
    v.flag("truePeak", p.truePeak);
    v.num("reverbRoomSize", p.reverbRoomSize);
    v.num("reverbDamping", p.reverbDamping);
    v.num("reverbWidth", p.reverbWidth);
    v.num("reverbReturn", p.reverbReturn);
}

template <class V> void visitRouting(V& v, RoutingParams& p)
{
    static const char* ch[8] = {"ch1", "ch2", "ch3", "ch4", "ch5", "ch6", "ch7", "ch8"};
    static const char* hp[8] = {"hp1", "hp2", "hp3", "hp4", "hp5", "hp6", "hp7", "hp8"};
    v.sub("channels", [&](auto& cs) {
        for (size_t i = 0; i < 8; ++i)
            cs.sub(ch[i], [&](auto& c) {
                c.num("fader", p.channel[i].fader);
                c.num("pan", p.channel[i].pan);
                c.flag("mute", p.channel[i].mute);
            });
    });
    v.sub("headphones", [&](auto& hs) {
        for (size_t i = 0; i < 8; ++i)
            hs.sub(hp[i], [&](auto& h) {
                h.en("mode", p.headphones[i].mode, 3);
                h.num("volume", p.headphones[i].volume);
                h.flag("mute", p.headphones[i].mute);
                h.flag("preFader", p.headphones[i].preFader);
                h.flag("protectOn", p.headphones[i].protectOn);
                h.num("protectCeilingDb", p.headphones[i].protectCeilingDb);
            });
    });
    v.sub("monitor", [&](auto& m) {
        m.en("source", p.monitor.source, 11);
        m.flag("autoPfl", p.monitor.autoPfl);
        m.num("volume", p.monitor.volume);
        m.flag("mute", p.monitor.mute);
        m.flag("dim", p.monitor.dim);
        m.num("dimGain", p.monitor.dimGain);
        m.flag("mono", p.monitor.mono);
        m.num("maxCeilingDb", p.monitor.maxCeilingDb);
    });
    v.num("masterFader", p.masterFader);
    v.flag("masterMute", p.masterMute);
    v.sub("matrix", [&](auto& mx) {
        for (int s = 0; s < kSourceCount; ++s)
            mx.sub(toString(static_cast<SourceId>(s)), [&](auto& row) {
                for (int b = 0; b < kBusCount; ++b)
                {
                    const auto bus = static_cast<BusId>(b);
                    if (bus == BusId::Pfl || bus == BusId::Monitor) continue; // not matrix-controlled
                    row.num(toString(bus), p.gain[static_cast<size_t>(s)][static_cast<size_t>(b)]);
                }
            });
    });
    v.sub("sourcePan", [&](auto& sp) {
        for (int s = 0; s < kSourceCount; ++s) sp.num(toString(static_cast<SourceId>(s)), p.sourcePan[static_cast<size_t>(s)]);
    });
    v.sub("talkback", [&](auto& t) {
        t.flag("toProgram", p.talkbackToProgram);
        t.num("level", p.talkbackLevel);
        t.flag("dim", p.talkbackDim);
        t.integer("source", p.talkbackSource, -1, 7);
        t.num("micGain", p.talkbackMicGain);
        t.sub("targets", [&](auto& ts) {
            for (size_t i = 0; i < 8; ++i) ts.flag(hp[i], p.talkbackTarget[i]);
        });
    });
}

template <class V> void visitDucker(V& v, dsp::DuckerParams& p)
{
    v.flag("on", p.on);
    v.num("thresholdDb", p.thresholdDb);
    v.num("depthDb", p.depthDb);
    v.num("attackMs", p.attackMs);
    v.num("holdMs", p.holdMs);
    v.num("releaseMs", p.releaseMs);
}

std::string pathUtf8(const std::filesystem::path& p) { return paths::utf8(p); }
std::filesystem::path pathFromUtf8(const std::string& s) { return paths::fromUtf8(s); }

const char* formatId(FileFormat f) { return f == FileFormat::Flac ? "flac" : f == FileFormat::Bwf ? "bwf" : "wav"; }

} // namespace

json::Value captureState(EngineController& c)
{
    auto& e = c.engine();
    json::Object root;
    root["version"] = kStateVersion;

    auto devices = json::parse(toJson(c.assignments()));
    root["devices"] = devices ? *devices : json::Value();

    json::Array dsp;
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        json::Object o;
        Writer w{o};
        visitDsp(w, e.dsp(ch));
        o["recordArm"] = e.recordArm(ch).load();
        dsp.emplace_back(std::move(o));
    }
    root["dsp"] = std::move(dsp);
    {
        json::Object o;
        Writer w{o};
        visitMasterDsp(w, e.masterDsp());
        root["master"] = json::Value(std::move(o));
    }
    {
        json::Object o;
        Writer w{o};
        visitRouting(w, e.routing());
        root["routing"] = json::Value(std::move(o));
    }
    root["talkbackKeyMode"] = static_cast<int>(c.talkbackKey().mode());

    const auto& rs = c.recordingSettings();
    json::Object rec;
    rec["format"] = formatId(rs.format);
    rec["bitDepth"] = static_cast<int>(rs.depth);
    rec["recordMain"] = rs.recordMain;
    rec["recordMusic"] = rs.recordMusic;
    rec["prerollSeconds"] = e.preroll() ? e.preroll()->seconds() : 0.0;
    root["recording"] = json::Value(std::move(rec));

    json::Array carts;
    auto& sb = e.soundboard();
    for (int i = 0; i < kCartCount; ++i)
    {
        auto& s = sb.settings(i);
        json::Object o;
        o["name"] = s.name;
        o["colour"] = s.colour;
        o["hotkey"] = s.hotkey;
        o["volume"] = static_cast<double>(s.volume.get());
        o["fadeInMs"] = static_cast<double>(s.fadeInMs.get());
        o["fadeOutMs"] = static_cast<double>(s.fadeOutMs.get());
        o["loop"] = s.loop.load();
        const auto buf = sb.buffer(i);
        o["file"] = buf && buf->error.empty() ? buf->sourcePath : std::string();
        carts.emplace_back(std::move(o));
    }
    root["carts"] = std::move(carts);

    auto& mp = e.music();
    json::Object music;
    json::Array playlist;
    for (const auto& t : mp.playlist()) playlist.emplace_back(pathUtf8(t.path));
    music["playlist"] = std::move(playlist);
    music["volume"] = static_cast<double>(mp.volume().get());
    music["autoAdvance"] = mp.autoAdvance().load();
    {
        json::Object d;
        Writer w{d};
        visitDucker(w, e.ducker());
        music["ducking"] = json::Value(std::move(d));
    }
    root["music"] = json::Value(std::move(music));
    return json::Value(std::move(root));
}

ApplyResult applyState(const json::Value& state, EngineController& c)
{
    ApplyResult result;
    if (!state.isObject()) return result;
    auto& e = c.engine();

    if (state["devices"].isObject())
        if (auto a = assignmentsFromJson(json::serialize(state["devices"]))) c.applyAssignments(*a);

    const auto& dsp = state["dsp"].asArray();
    for (int ch = 0; ch < kNumChannels && static_cast<size_t>(ch) < dsp.size(); ++ch)
    {
        Reader r{dsp[static_cast<size_t>(ch)]};
        visitDsp(r, e.dsp(ch));
        r.flag("recordArm", e.recordArm(ch));
    }
    {
        Reader r{state["master"]};
        visitMasterDsp(r, e.masterDsp());
    }
    {
        Reader r{state["routing"]};
        visitRouting(r, e.routing());
    }
    if (const int m = state["talkbackKeyMode"].asInt(-1); m >= 0 && m <= 2)
    {
        c.talkbackKey().reset();
        c.talkbackKey().setMode(static_cast<TalkbackMode>(m));
        e.routing().talkbackActive = false;
    }

    if (const auto& rec = state["recording"]; rec.isObject())
    {
        auto& rs = c.recordingSettings();
        const auto f = rec["format"].asString();
        if (f == "wav") rs.format = FileFormat::Wav;
        else if (f == "bwf") rs.format = FileFormat::Bwf;
        else if (f == "flac") rs.format = FileFormat::Flac;
        const int d = rec["bitDepth"].asInt(0);
        if (d == 16 || d == 24 || d == 32) rs.depth = static_cast<BitDepth>(d);
        rs.recordMain = rec["recordMain"].asBool(rs.recordMain);
        rs.recordMusic = rec["recordMusic"].asBool(rs.recordMusic);
        const double pre = rec["prerollSeconds"].asNumber(-1.0);
        if (pre == 0.0 || pre == 5.0 || pre == 10.0 || pre == 30.0 || pre == 60.0)
            if (!e.setPrerollSeconds(pre)) result.warnings.push_back("pre-roll not changed while recording");
    }

    const auto& carts = state["carts"].asArray();
    auto& sb = e.soundboard();
    for (int i = 0; i < kCartCount && static_cast<size_t>(i) < carts.size(); ++i)
    {
        const auto& o = carts[static_cast<size_t>(i)];
        if (!o.isObject()) continue;
        auto& s = sb.settings(i);
        s.name = o["name"].asString();
        s.colour = o["colour"].asString(s.colour);
        s.hotkey = o["hotkey"].asString();
        Reader r{o};
        r.num("volume", s.volume);
        r.num("fadeInMs", s.fadeInMs);
        r.num("fadeOutMs", s.fadeOutMs);
        r.flag("loop", s.loop);
        sb.stop(i);
        const std::string file = o["file"].asString();
        if (file.empty())
            sb.setBuffer(i, nullptr);
        else
        {
            const auto path = pathFromUtf8(file);
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) result.carts.emplace_back(i, path);
            else
            {
                sb.setBuffer(i, nullptr);
                result.warnings.push_back("Cart " + std::to_string(i + 1) + ": file not found - " + file);
            }
        }
    }

    if (const auto& music = state["music"]; music.isObject())
    {
        auto& mp = e.music();
        std::vector<std::filesystem::path> files;
        for (const auto& f : music["playlist"].asArray())
        {
            const auto path = pathFromUtf8(f.asString());
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) result.warnings.push_back("Music: file not found - " + f.asString());
            files.push_back(path); // kept in the list (marked unreadable when played) so the order survives
        }
        mp.stop();
        mp.setPlaylist(std::move(files));
        Reader r{music};
        r.num("volume", mp.volume());
        r.flag("autoAdvance", mp.autoAdvance());
        Reader d{music["ducking"]};
        visitDucker(d, e.ducker());
    }
    return result;
}

} // namespace pf8::project
