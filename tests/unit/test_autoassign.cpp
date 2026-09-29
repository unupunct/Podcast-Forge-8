#include <catch2/catch_test_macros.hpp>

#include "devices/AutoAssign.h"

using namespace pf8;

namespace {
DeviceInfo dev(std::string id, Flow flow, DeviceKind kind, std::string name, std::string container, bool paired = false,
               bool online = true)
{
    DeviceInfo d;
    d.raw.endpointId = std::move(id);
    d.raw.flow = flow;
    d.raw.friendlyName = std::move(name);
    d.raw.containerId = std::move(container);
    d.raw.state = online ? DeviceState::Active : DeviceState::Unplugged;
    d.kind = kind;
    d.pairedInContainer = paired;
    d.usb = UsbId{1, 2, "S" + d.raw.endpointId, true};
    return d;
}
} // namespace

TEST_CASE("AutoAssign fills only empty cells and pairs headsets", "[devices][autoassign]")
{
    std::vector<DeviceInfo> devices = {
        dev("mic-A", Flow::Capture, DeviceKind::UsbMicrophone, "A Mic", "{a}"),
        dev("hs-in", Flow::Capture, DeviceKind::UsbHeadset, "Headset", "{h}", true),
        dev("hs-out", Flow::Render, DeviceKind::UsbHeadset, "Headset", "{h}", true),
        dev("hp-B", Flow::Render, DeviceKind::UsbHeadphones, "B Phones", "{b}"),
        dev("rtk", Flow::Render, DeviceKind::BuiltIn, "Realtek", "{pc}"),
        dev("vb", Flow::Capture, DeviceKind::Virtual, "CABLE Output", "{vb}"),
        dev("gone", Flow::Capture, DeviceKind::UsbMicrophone, "Offline Mic", "{g}", false, false),
    };
    Assignments a = Assignments::defaults();
    a.ch[0].mic = identityOf(devices[0]); // CH1 already has "A Mic"

    auto p = proposeAutoAssign(a, devices);
    // The headset goes to the first channel with BOTH cells empty (CH2), B Phones to CH1's empty
    // headphone cell. Built-in, virtual, offline and already-used devices are never proposed.
    REQUIRE(p.changes.size() == 3);
    CHECK(p.changes[0].channel == 0);
    CHECK_FALSE(p.changes[0].mic);
    CHECK(p.changes[0].endpointId == "hp-B");
    CHECK(p.changes[1].channel == 1);
    CHECK(p.changes[1].mic);
    CHECK(p.changes[1].endpointId == "hs-in");
    CHECK(p.changes[2].channel == 1);
    CHECK(p.changes[2].endpointId == "hs-out");
    for (const auto& c : p.changes) CHECK_FALSE((c.channel == 0 && c.mic)); // never touches an assigned cell

    auto applied = applyProposal(a, p, devices);
    CHECK(applied.ch[0].mic->endpointId == "mic-A");
    CHECK(applied.ch[1].mic->endpointId == "hs-in");
    CHECK(applied.ch[1].headphones->endpointId == "hs-out");
    CHECK(applied.ch[0].headphones->endpointId == "hp-B");

    // Running it again proposes nothing new.
    CHECK(proposeAutoAssign(applied, devices).empty());
}

TEST_CASE("AutoAssign apply never overwrites a cell assigned after the proposal", "[devices][autoassign]")
{
    std::vector<DeviceInfo> devices = {dev("mic-A", Flow::Capture, DeviceKind::UsbMicrophone, "A", "{a}"),
                                       dev("mic-B", Flow::Capture, DeviceKind::UsbMicrophone, "B", "{b}")};
    Assignments a = Assignments::defaults();
    auto p = proposeAutoAssign(a, devices);
    REQUIRE(p.changes.size() == 2);
    a.ch[0].mic = identityOf(devices[1]); // user assigned CH1 manually before confirming
    auto applied = applyProposal(a, p, devices);
    CHECK(applied.ch[0].mic->endpointId == "mic-B");
}
