#pragma once
// Audio endpoint description, USB identity and device-kind classification.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf8 {

enum class Flow : uint8_t { Capture, Render };
enum class DeviceState : uint8_t { Active, Disabled, Unplugged, NotPresent };
enum class DeviceKind : uint8_t
{
    UsbMicrophone, UsbHeadset, UsbInterface, UsbHeadphones, BuiltIn, Hdmi, Virtual, Bluetooth, Other
};
enum class FormFactor : uint8_t
{
    Unknown, Speakers, LineLevel, Headphones, Microphone, Headset, Handset, Digital, Spdif, Hdmi, Other
};

struct UsbId
{
    uint16_t vid = 0;
    uint16_t pid = 0;
    std::string instance;   // instance segment of the interface path
    bool hasSerial = false; // false when Windows generated a port-derived instance id
    bool operator==(const UsbId&) const = default;
};

// Parses "\\?\usb#vid_046d&pid_0a38&mi_00#7&2a1b3c&0&0000#{guid}" (case-insensitive).
std::optional<UsbId> parseUsbInterfacePath(std::string_view path);
// Parses a device instance id "USB\VID_046D&PID_085E\<instance>" (case-insensitive).
std::optional<UsbId> parseUsbInstanceId(std::string_view id);

// Exactly what the OS enumerator reports for one endpoint.
struct EndpointRaw
{
    std::string endpointId;
    std::string friendlyName;
    std::string deviceDesc;
    std::string manufacturer;
    std::string enumerator;          // "USB", "HDAUDIO", "ROOT", "SWD", "BTHENUM", ...
    std::string containerId;
    std::string parentInterfacePath; // from IDeviceTopology
    std::string usbDeviceInstanceId; // nearest USB device node above the audio function, e.g. "USB\\VID_046D&PID_085E\\7A1B2C3D"
    Flow flow = Flow::Render;
    DeviceState state = DeviceState::NotPresent;
    FormFactor formFactor = FormFactor::Unknown;
    int mixRate = 0;
    int mixChannels = 0;
    std::vector<int> exclusiveRates;
    bool operator==(const EndpointRaw&) const = default;
};

struct DeviceInfo
{
    EndpointRaw raw;
    DeviceKind kind = DeviceKind::Other;
    std::optional<UsbId> usb;
    bool pairedInContainer = false; // capture and render share a container (headset)

    bool online() const noexcept { return raw.state == DeviceState::Active; }
    std::string displayName() const;
};

std::vector<DeviceInfo> classify(const std::vector<EndpointRaw>& endpoints);

const char* toString(DeviceKind k) noexcept;
const char* toString(DeviceState s) noexcept;
const char* toString(Flow f) noexcept;

} // namespace pf8
