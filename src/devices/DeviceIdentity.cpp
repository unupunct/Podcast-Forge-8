#include "devices/DeviceIdentity.h"

#include <algorithm>
#include <cctype>

#include "core/Json.h"

namespace pf8 {
namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool sameDevice(const DeviceIdentity& a, const DeviceInfo& d)
{
    return d.raw.flow == a.flow && d.usb && d.usb->vid == a.vid && d.usb->pid == a.pid;
}

json::Value identityToJson(const DeviceIdentity& id)
{
    json::Object o;
    o["endpointId"] = id.endpointId;
    o["flow"] = id.flow == Flow::Capture ? "capture" : "render";
    o["containerId"] = id.containerId;
    o["vid"] = static_cast<int>(id.vid);
    o["pid"] = static_cast<int>(id.pid);
    o["serial"] = id.serial;
    o["name"] = id.friendlyName;
    return json::Value(std::move(o));
}

std::optional<DeviceIdentity> identityFromJson(const json::Value& v)
{
    if (!v.isObject() || !v["endpointId"].isString()) return std::nullopt;
    DeviceIdentity id;
    id.endpointId = v["endpointId"].asString();
    id.flow = v["flow"].asString() == "capture" ? Flow::Capture : Flow::Render;
    id.containerId = v["containerId"].asString();
    id.vid = static_cast<uint16_t>(v["vid"].asInt());
    id.pid = static_cast<uint16_t>(v["pid"].asInt());
    id.serial = v["serial"].asString();
    id.friendlyName = v["name"].asString();
    return id;
}

} // namespace

DeviceIdentity identityOf(const DeviceInfo& d)
{
    DeviceIdentity id;
    id.endpointId = d.raw.endpointId;
    id.flow = d.raw.flow;
    id.containerId = d.raw.containerId;
    id.friendlyName = d.raw.friendlyName;
    if (d.usb)
    {
        id.vid = d.usb->vid;
        id.pid = d.usb->pid;
        if (d.usb->hasSerial) id.serial = d.usb->instance;
    }
    return id;
}

const char* toString(MatchKind k) noexcept
{
    switch (k)
    {
        case MatchKind::Exact:         return "exact";
        case MatchKind::Fingerprint:   return "fingerprint";
        case MatchKind::PossibleMatch: return "possible match";
        case MatchKind::None:          return "none";
    }
    return "none";
}

Resolution resolve(const DeviceIdentity& saved, const std::vector<DeviceInfo>& devices, const std::set<std::string>& taken)
{
    // 1. Exact endpoint id, active.
    for (const auto& d : devices)
        if (d.online() && d.raw.endpointId == saved.endpointId && !taken.count(d.raw.endpointId))
            return {MatchKind::Exact, d.raw.endpointId};

    // The exact endpoint exists but is offline: the device is unplugged — wait for it.
    // (A fingerprint match elsewhere could be a *second* identical device.)
    const bool exactKnownOffline = std::any_of(devices.begin(), devices.end(), [&](const DeviceInfo& d) {
        return d.raw.endpointId == saved.endpointId && !d.online();
    });

    if (saved.vid == 0 && saved.pid == 0) return {};

    // 2. Fingerprint with a real serial: unique active device with the same flow + VID/PID + serial.
    if (!saved.serial.empty())
    {
        std::vector<const DeviceInfo*> hits;
        for (const auto& d : devices)
            if (d.online() && sameDevice(saved, d) && d.usb->hasSerial && lower(d.usb->instance) == lower(saved.serial) &&
                !taken.count(d.raw.endpointId))
                hits.push_back(&d);
        if (hits.size() == 1) return {MatchKind::Fingerprint, hits.front()->raw.endpointId};
    }
    if (exactKnownOffline) return {};

    // 3. Weak fingerprint: same flow + VID/PID + name, no usable serial → never automatic.
    for (const auto& d : devices)
        if (d.online() && sameDevice(saved, d) && d.raw.friendlyName == saved.friendlyName && !taken.count(d.raw.endpointId))
            return {MatchKind::PossibleMatch, d.raw.endpointId};
    return {};
}

const char* toString(OutputRole r) noexcept
{
    switch (r)
    {
        case OutputRole::Monitor:     return "Monitor";
        case OutputRole::MainStream:  return "Stream: Main";
        case OutputRole::CleanStream: return "Stream: Clean feed";
        case OutputRole::MusicStream: return "Stream: Music";
    }
    return "?";
}

Assignments Assignments::defaults()
{
    Assignments a;
    const char* names[8] = {"Host 1", "Host 2", "Guest 1", "Guest 2", "Guest 3", "Guest 4", "Guest 5", "Guest 6"};
    for (size_t i = 0; i < 8; ++i) a.ch[i].name = names[i];
    return a;
}

std::string toJson(const Assignments& a)
{
    json::Object root;
    root["version"] = 1;
    root["preferredMaster"] = a.preferredMaster;
    json::Array channels;
    for (const auto& c : a.ch)
    {
        json::Object o;
        o["name"] = c.name;
        o["mic"] = c.mic ? identityToJson(*c.mic) : json::Value();
        o["micChannel"] = c.micChannel;
        o["headphones"] = c.headphones ? identityToJson(*c.headphones) : json::Value();
        o["hpPair"] = c.hpPair;
        channels.emplace_back(std::move(o));
    }
    root["channels"] = std::move(channels);
    json::Array outs;
    for (const auto& o : a.outputs)
    {
        json::Object j;
        j["device"] = o.device ? identityToJson(*o.device) : json::Value();
        j["pair"] = o.pair;
        outs.emplace_back(std::move(j));
    }
    root["outputs"] = std::move(outs);
    return json::serialize(json::Value(std::move(root)), true);
}

std::optional<Assignments> assignmentsFromJson(const std::string& text)
{
    auto v = json::parse(text);
    if (!v || !v->isObject() || !(*v)["channels"].isArray()) return std::nullopt;
    Assignments a = Assignments::defaults();
    a.preferredMaster = (*v)["preferredMaster"].asString();
    const auto& arr = (*v)["channels"].asArray();
    for (size_t i = 0; i < a.ch.size() && i < arr.size(); ++i)
    {
        const auto& c = arr[i];
        if (c["name"].isString()) a.ch[i].name = c["name"].asString();
        a.ch[i].mic = identityFromJson(c["mic"]);
        a.ch[i].micChannel = c["micChannel"].asInt(-1);
        a.ch[i].headphones = identityFromJson(c["headphones"]);
        a.ch[i].hpPair = c["hpPair"].asInt(0);
    }
    const auto& outs = (*v)["outputs"].asArray();
    for (size_t i = 0; i < a.outputs.size() && i < outs.size(); ++i)
    {
        a.outputs[i].device = identityFromJson(outs[i]["device"]);
        a.outputs[i].pair = outs[i]["pair"].asInt(0);
    }
    return a;
}

std::array<Resolution, kOutputRoles> resolveOutputs(const Assignments& a, const std::vector<DeviceInfo>& devices)
{
    std::array<Resolution, kOutputRoles> out{};
    for (size_t i = 0; i < out.size(); ++i)
        if (a.outputs[i].device) out[i] = resolve(*a.outputs[i].device, devices, {});
    return out;
}

std::array<ChannelResolution, 8> resolveAll(const Assignments& a, const std::vector<DeviceInfo>& devices)
{
    std::array<ChannelResolution, 8> out{};
    // Exact matches first across all channels, so a fingerprint can never steal an exact endpoint.
    std::set<std::string> micTaken, hpTaken;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t i = 0; i < 8; ++i)
        {
            const auto& c = a.ch[i];
            if (c.mic && out[i].mic.kind != MatchKind::Exact && out[i].mic.kind != MatchKind::Fingerprint)
            {
                auto r = resolve(*c.mic, devices, micTaken);
                if (pass == 0 && r.kind != MatchKind::Exact) r = {};
                if (r.kind == MatchKind::Exact || r.kind == MatchKind::Fingerprint)
                {
                    // An interface can feed several channels on different inputs: only the same
                    // endpoint + same input channel is exclusive.
                    bool clash = false;
                    for (size_t j = 0; j < 8; ++j)
                        if (j != i && (out[j].mic.kind == MatchKind::Exact || out[j].mic.kind == MatchKind::Fingerprint) &&
                            out[j].mic.endpointId == r.endpointId && a.ch[j].micChannel == c.micChannel)
                            clash = true;
                    if (!clash) out[i].mic = r;
                }
                else if (pass == 1)
                    out[i].mic = r;
            }
            if (c.headphones && out[i].headphones.kind != MatchKind::Exact && out[i].headphones.kind != MatchKind::Fingerprint)
            {
                auto r = resolve(*c.headphones, devices, hpTaken);
                if (pass == 0 && r.kind != MatchKind::Exact) r = {};
                if (r.kind == MatchKind::Exact || r.kind == MatchKind::Fingerprint)
                {
                    bool clash = false;
                    for (size_t j = 0; j < 8; ++j)
                        if (j != i && (out[j].headphones.kind == MatchKind::Exact || out[j].headphones.kind == MatchKind::Fingerprint) &&
                            out[j].headphones.endpointId == r.endpointId && a.ch[j].hpPair == c.hpPair)
                            clash = true;
                    if (!clash) out[i].headphones = r;
                }
                else if (pass == 1)
                    out[i].headphones = r;
            }
        }
    }
    return out;
}

} // namespace pf8
