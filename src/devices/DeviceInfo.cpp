#include "devices/DeviceInfo.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace pf8 {
namespace {

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains(std::string_view hay, std::string_view needle)
{
    return lower(hay).find(lower(needle)) != std::string::npos;
}

std::optional<uint16_t> hex16(std::string_view s)
{
    if (s.size() != 4) return std::nullopt;
    uint16_t v = 0;
    for (char c : s)
    {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<uint16_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint16_t>(c - 'a' + 10);
        else return std::nullopt;
    }
    return v;
}

bool isVirtualVendor(const EndpointRaw& e)
{
    static const char* names[] = {"vb-audio", "voicemeeter", "virtual", "vac ", "blackhole", "obs"};
    for (const char* n : names)
        if (contains(e.manufacturer, n) || contains(e.friendlyName, n) || contains(e.deviceDesc, n)) return true;
    return false;
}

bool looksHdmi(const EndpointRaw& e)
{
    if (e.formFactor == FormFactor::Hdmi || e.formFactor == FormFactor::Digital) return true;
    for (const char* n : {"hdmi", "displayport", "display audio", "nvidia high definition", "amd high definition"})
        if (contains(e.friendlyName, n) || contains(e.deviceDesc, n)) return true;
    return false;
}

} // namespace

std::optional<UsbId> parseUsbInterfacePath(std::string_view path)
{
    const std::string p = lower(path);
    const size_t usb = p.find("usb#vid_");
    if (usb == std::string::npos) return std::nullopt;
    const size_t vidPos = usb + 8;
    const size_t pidTag = p.find("&pid_", vidPos);
    if (pidTag != vidPos + 4) return std::nullopt;
    auto vid = hex16(std::string_view(p).substr(vidPos, 4));
    auto pid = hex16(std::string_view(p).substr(pidTag + 5, 4));
    if (!vid || !pid) return std::nullopt;

    UsbId id;
    id.vid = *vid;
    id.pid = *pid;
    // Instance segment: between the next two '#' after the vid/pid block. Keep original casing.
    const size_t hash1 = p.find('#', pidTag);
    if (hash1 != std::string::npos)
    {
        const size_t hash2 = p.find('#', hash1 + 1);
        id.instance = std::string(path.substr(hash1 + 1, (hash2 == std::string::npos ? p.size() : hash2) - hash1 - 1));
    }
    // Windows-generated instance ids contain '&' (e.g. "7&2a1b3c&0&0000"); real serials don't.
    id.hasSerial = !id.instance.empty() && id.instance.find('&') == std::string::npos;
    return id;
}

std::optional<UsbId> parseUsbInstanceId(std::string_view id)
{
    const std::string p = lower(id);
    if (p.rfind("usb\\vid_", 0) != 0 || p.size() < 21 || p.compare(12, 5, "&pid_") != 0) return std::nullopt;
    auto vid = hex16(std::string_view(p).substr(8, 4));
    auto pid = hex16(std::string_view(p).substr(17, 4));
    if (!vid || !pid) return std::nullopt;
    UsbId u;
    u.vid = *vid;
    u.pid = *pid;
    const size_t slash = p.find('\\', 21);
    if (slash != std::string::npos) u.instance = std::string(id.substr(slash + 1));
    u.hasSerial = !u.instance.empty() && u.instance.find('&') == std::string::npos;
    return u;
}

std::string DeviceInfo::displayName() const
{
    std::string name = raw.friendlyName.empty() ? raw.deviceDesc : raw.friendlyName;
    if (!online()) name += " [OFFLINE]";
    return name;
}

std::vector<DeviceInfo> classify(const std::vector<EndpointRaw>& endpoints)
{
    // Which containers have both a capture and a render endpoint (headsets)?
    std::map<std::string, std::pair<bool, bool>> flows; // containerId -> (hasCapture, hasRender)
    for (const auto& e : endpoints)
    {
        if (e.containerId.empty()) continue;
        auto& f = flows[lower(e.containerId)];
        (e.flow == Flow::Capture ? f.first : f.second) = true;
    }

    std::vector<DeviceInfo> out;
    out.reserve(endpoints.size());
    for (const auto& e : endpoints)
    {
        DeviceInfo d;
        d.raw = e;
        d.usb = parseUsbInterfacePath(e.parentInterfacePath);
        // Composite devices (webcam mics, headsets with HID): the serial lives on the parent USB
        // device node, not on the audio interface.
        if (d.usb && !d.usb->hasSerial)
            if (auto parent = parseUsbInstanceId(e.usbDeviceInstanceId))
                if (parent->vid == d.usb->vid && parent->pid == d.usb->pid && parent->hasSerial)
                {
                    d.usb->instance = parent->instance;
                    d.usb->hasSerial = true;
                }
        if (!e.containerId.empty())
        {
            const auto& f = flows[lower(e.containerId)];
            d.pairedInContainer = f.first && f.second;
        }

        const std::string en = lower(e.enumerator);
        const bool usb = en == "usb" || d.usb.has_value();
        if (usb)
        {
            const bool manyChannels = e.mixChannels >= 3;
            if (manyChannels || e.formFactor == FormFactor::LineLevel || e.formFactor == FormFactor::Spdif)
                d.kind = DeviceKind::UsbInterface;
            else if (d.pairedInContainer)
                d.kind = DeviceKind::UsbHeadset;
            else if (e.flow == Flow::Capture)
                d.kind = DeviceKind::UsbMicrophone;
            else
                d.kind = DeviceKind::UsbHeadphones;
        }
        else if (en == "bthenum" || en == "bthledevice" || en == "bthhfenum")
            d.kind = DeviceKind::Bluetooth;
        else if (isVirtualVendor(e) || en == "root" || en == "swd")
            d.kind = DeviceKind::Virtual;
        else if ((en == "hdaudio" || en == "intelaudio" || en.empty()) && looksHdmi(e))
            d.kind = DeviceKind::Hdmi;
        else if (en == "hdaudio" || en == "intelaudio" || en == "pci" || en == "acpi")
            d.kind = DeviceKind::BuiltIn;
        else
            d.kind = DeviceKind::Other;

        out.push_back(std::move(d));
    }
    return out;
}

const char* toString(DeviceKind k) noexcept
{
    switch (k)
    {
        case DeviceKind::UsbMicrophone: return "USB microphone";
        case DeviceKind::UsbHeadset:    return "USB headset";
        case DeviceKind::UsbInterface:  return "USB audio interface";
        case DeviceKind::UsbHeadphones: return "USB headphones";
        case DeviceKind::BuiltIn:       return "Built-in";
        case DeviceKind::Hdmi:          return "HDMI / DisplayPort";
        case DeviceKind::Virtual:       return "Virtual";
        case DeviceKind::Bluetooth:     return "Bluetooth";
        case DeviceKind::Other:         return "Other";
    }
    return "Other";
}

const char* toString(DeviceState s) noexcept
{
    switch (s)
    {
        case DeviceState::Active:     return "Active";
        case DeviceState::Disabled:   return "Disabled";
        case DeviceState::Unplugged:  return "Disconnected";
        case DeviceState::NotPresent: return "Not present";
    }
    return "Unknown";
}

const char* toString(Flow f) noexcept { return f == Flow::Capture ? "Input" : "Output"; }

} // namespace pf8
