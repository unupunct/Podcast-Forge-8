#include "app/CliModes.h"

#include <windows.h>
#include <objbase.h>

#include "devices/DeviceInfo.h"
#include "devices/WinEndpointEnumerator.h"

namespace pf8::cli {
namespace {

juce::String u8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

juce::var deviceToVar(const DeviceInfo& d)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("name", u8(d.raw.friendlyName));
    o->setProperty("description", u8(d.raw.deviceDesc));
    o->setProperty("manufacturer", u8(d.raw.manufacturer));
    o->setProperty("type", juce::String(toString(d.kind)));
    o->setProperty("direction", juce::String(toString(d.raw.flow)));
    o->setProperty("status", juce::String(toString(d.raw.state)));
    o->setProperty("endpointId", u8(d.raw.endpointId));
    o->setProperty("containerId", u8(d.raw.containerId));
    o->setProperty("enumerator", u8(d.raw.enumerator));
    o->setProperty("mixRate", d.raw.mixRate);
    o->setProperty("channels", d.raw.mixChannels);
    juce::Array<juce::var> rates;
    for (int r : d.raw.exclusiveRates) rates.add(r);
    o->setProperty("exclusiveRates", rates);
    o->setProperty("headsetPair", d.pairedInContainer);
    if (d.usb)
    {
        auto* u = new juce::DynamicObject();
        u->setProperty("vid", juce::String::toHexString(d.usb->vid).paddedLeft('0', 4));
        u->setProperty("pid", juce::String::toHexString(d.usb->pid).paddedLeft('0', 4));
        u->setProperty("instance", u8(d.usb->instance));
        u->setProperty("hasSerial", d.usb->hasSerial);
        o->setProperty("usb", juce::var(u));
    }
    return juce::var(o);
}

int listDevices()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    juce::Array<juce::var> arr;
    for (const auto& d : classify(enumerateEndpoints())) arr.add(deviceToVar(d));
    CoUninitialize();
    writeStdout(juce::JSON::toString(juce::var(arr), false) + "\n");
    return 0;
}

} // namespace

void writeStdout(const juce::String& text)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    const auto utf8 = text.toStdString();
    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

std::optional<int> runHeadless(const juce::StringArray& args)
{
    if (args.contains("--list-devices")) return listDevices();
    if (args.contains("--help") || args.contains("-h"))
    {
        writeStdout("Podcast Forge 8\n"
                    "  --list-devices   print all audio endpoints as JSON\n"
                    "  --help           this text\n");
        return 0;
    }
    return std::nullopt;
}

} // namespace pf8::cli
