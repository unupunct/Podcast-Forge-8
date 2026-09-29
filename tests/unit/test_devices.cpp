#include <catch2/catch_test_macros.hpp>

#include "devices/DeviceInfo.h"
#include "devices/DeviceRegistry.h"

using namespace pf8;

TEST_CASE("parseUsbInterfacePath extracts vid, pid and instance", "[devices]")
{
    auto id = parseUsbInterfacePath(R"(\\?\usb#vid_046d&pid_0a38&mi_00#7&2a1b3c&0&0000#{6994ad04-93ef-11d0-a3cc-00a0c9223196})");
    REQUIRE(id.has_value());
    CHECK(id->vid == 0x046D);
    CHECK(id->pid == 0x0A38);
    CHECK(id->instance == "7&2a1b3c&0&0000");
    CHECK_FALSE(id->hasSerial);

    auto serial = parseUsbInterfacePath(R"(\\?\USB#VID_17A0&PID_0305#A1B2C3D4E5#{6994ad04-93ef-11d0-a3cc-00a0c9223196})");
    REQUIRE(serial.has_value());
    CHECK(serial->vid == 0x17A0);
    CHECK(serial->pid == 0x0305);
    CHECK(serial->instance == "A1B2C3D4E5");
    CHECK(serial->hasSerial);

    CHECK_FALSE(parseUsbInterfacePath(R"(\\?\hdaudio#func_01&ven_10ec&dev_0256#4&1a2b&0&0001#{guid})").has_value());
    CHECK_FALSE(parseUsbInterfacePath("").has_value());
}

namespace {
EndpointRaw ep(std::string id, std::string name, std::string enumerator, Flow flow, std::string container,
               std::string parent = {}, std::string mfg = {}, FormFactor ff = FormFactor::Unknown, int ch = 2)
{
    EndpointRaw e;
    e.endpointId = std::move(id);
    e.friendlyName = std::move(name);
    e.enumerator = std::move(enumerator);
    e.flow = flow;
    e.containerId = std::move(container);
    e.parentInterfacePath = std::move(parent);
    e.manufacturer = std::move(mfg);
    e.formFactor = ff;
    e.state = DeviceState::Active;
    e.mixRate = 48000;
    e.mixChannels = ch;
    return e;
}
} // namespace

TEST_CASE("classify recognises the common device kinds", "[devices]")
{
    std::vector<EndpointRaw> eps = {
        ep("{0.0.1.00000000}.{yeti}", "Microphone (Yeti Stereo Microphone)", "USB", Flow::Capture, "{c-yeti}",
           R"(\\?\usb#vid_b58e&pid_9e84&mi_00#6&abc&0&0000#{g})", "Blue", FormFactor::Microphone),
        ep("{0.0.1.00000000}.{hx-in}", "Microphone (HyperX Cloud)", "USB", Flow::Capture, "{c-hx}",
           R"(\\?\usb#vid_0951&pid_16a4&mi_00#6&def&0&0000#{g})", "HP", FormFactor::Headset, 1),
        ep("{0.0.0.00000000}.{hx-out}", "Speakers (HyperX Cloud)", "USB", Flow::Render, "{c-hx}",
           R"(\\?\usb#vid_0951&pid_16a4&mi_00#6&def&0&0000#{g})", "HP", FormFactor::Headset),
        ep("{0.0.0.00000000}.{rtk}", "Speakers (Realtek(R) Audio)", "HDAUDIO", Flow::Render, "{c-pc}", {},
           "Realtek", FormFactor::Speakers),
        ep("{0.0.0.00000000}.{nv}", "LG TV (NVIDIA High Definition Audio)", "HDAUDIO", Flow::Render, "{c-tv}", {},
           "NVIDIA", FormFactor::Digital),
        ep("{0.0.0.00000000}.{vb}", "CABLE Input (VB-Audio Virtual Cable)", "ROOT", Flow::Render, "{c-vb}", {},
           "VB-Audio Software"),
        ep("{0.0.1.00000000}.{if}", "Line (Scarlett 18i20)", "USB", Flow::Capture, "{c-if}",
           R"(\\?\usb#vid_1235&pid_8215#P9ABC123#{g})", "Focusrite", FormFactor::LineLevel, 18),
    };

    auto devs = classify(eps);
    REQUIRE(devs.size() == eps.size());
    CHECK(devs[0].kind == DeviceKind::UsbMicrophone);
    CHECK(devs[1].kind == DeviceKind::UsbHeadset);
    CHECK(devs[1].pairedInContainer);
    CHECK(devs[2].kind == DeviceKind::UsbHeadset);
    CHECK(devs[3].kind == DeviceKind::BuiltIn);
    CHECK(devs[4].kind == DeviceKind::Hdmi);
    CHECK(devs[5].kind == DeviceKind::Virtual);
    CHECK(devs[6].kind == DeviceKind::UsbInterface);
    CHECK(devs[6].usb->hasSerial);
}

TEST_CASE("displayName marks offline devices", "[devices]")
{
    DeviceInfo d;
    d.raw.friendlyName = "Shure MV7";
    d.raw.state = DeviceState::Unplugged;
    CHECK(d.displayName() == "Shure MV7 [OFFLINE]");
    d.raw.state = DeviceState::Active;
    CHECK(d.displayName() == "Shure MV7");
}

TEST_CASE("DeviceRegistry reports added, removed and changed endpoints", "[devices]")
{
    DeviceRegistry reg;
    std::vector<EndpointRaw> snap = {
        ep("a", "Mic A", "USB", Flow::Capture, "{1}"),
        ep("b", "Mic B", "USB", Flow::Capture, "{2}"),
        ep("c", "Headset C", "USB", Flow::Render, "{3}"),
    };
    auto d1 = reg.rebuild(snap);
    CHECK(d1.added.size() == 3);
    CHECK(d1.removed.empty());
    const auto gen = reg.generation();

    CHECK(reg.rebuild(snap).empty());
    CHECK(reg.generation() == gen);

    auto snap2 = snap;
    snap2.erase(snap2.begin());                   // a removed
    snap2[0].state = DeviceState::Unplugged;      // b changed
    auto d2 = reg.rebuild(snap2);
    REQUIRE(d2.removed.size() == 1);
    CHECK(d2.removed[0] == "a");
    REQUIRE(d2.changed.size() == 1);
    CHECK(d2.changed[0] == "b");
    CHECK(d2.added.empty());
    CHECK(reg.generation() == gen + 1);
    CHECK_FALSE(reg.find("a").has_value());
    CHECK(reg.find("b")->displayName() == "Mic B [OFFLINE]");
}
