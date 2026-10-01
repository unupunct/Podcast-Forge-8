#include <catch2/catch_test_macros.hpp>

#include "core/Json.h"
#include "devices/DeviceIdentity.h"

using namespace pf8;

namespace {
DeviceInfo usbDev(std::string id, Flow flow, uint16_t vid, uint16_t pid, std::string instance, bool serial,
                  std::string name, bool online = true, std::string container = "{c}")
{
    DeviceInfo d;
    d.raw.endpointId = std::move(id);
    d.raw.flow = flow;
    d.raw.friendlyName = std::move(name);
    d.raw.state = online ? DeviceState::Active : DeviceState::Unplugged;
    d.raw.containerId = std::move(container);
    d.usb = UsbId{vid, pid, std::move(instance), serial};
    d.kind = flow == Flow::Capture ? DeviceKind::UsbMicrophone : DeviceKind::UsbHeadphones;
    return d;
}
} // namespace

TEST_CASE("Json parses and serialises round-trip", "[core][json]")
{
    const std::string text = R"({"a":1,"b":[true,false,null],"c":"x\"y\\z\né🎙","d":-2.5e3,"e":{}})";
    std::string err;
    auto v = json::parse(text, &err);
    REQUIRE(v.has_value());
    CHECK((*v)["a"].asInt() == 1);
    CHECK((*v)["b"][0].asBool());
    CHECK((*v)["b"][2].isNull());
    CHECK((*v)["c"].asString() == "x\"y\\z\n\xc3\xa9\xf0\x9f\x8e\x99");
    CHECK((*v)["d"].asNumber() == -2500.0);
    CHECK((*v)["missing"]["deep"].isNull());
    auto again = json::parse(json::serialize(*v, true));
    REQUIRE(again.has_value());
    CHECK(json::serialize(*again) == json::serialize(*v));

    CHECK_FALSE(json::parse("{\"a\":}").has_value());
    CHECK_FALSE(json::parse("[1,2").has_value());
    CHECK_FALSE(json::parse("{} x").has_value());
}

TEST_CASE("resolve: exact endpoint id wins", "[devices][identity]")
{
    auto mic = usbDev("ep-A", Flow::Capture, 0x046d, 0x085e, "SER123", true, "Mic (BRIO)");
    auto saved = identityOf(mic);
    auto r = resolve(saved, {mic}, {});
    CHECK(r.kind == MatchKind::Exact);
    CHECK(r.endpointId == "ep-A");
}

TEST_CASE("resolve: same serial on a new endpoint id is a fingerprint match", "[devices][identity]")
{
    auto before = usbDev("ep-old", Flow::Capture, 0x17a0, 0x0305, "SER9", true, "Yeti");
    auto saved = identityOf(before);
    auto moved = usbDev("ep-new", Flow::Capture, 0x17a0, 0x0305, "SER9", true, "Yeti");
    auto r = resolve(saved, {moved}, {});
    CHECK(r.kind == MatchKind::Fingerprint);
    CHECK(r.endpointId == "ep-new");
}

TEST_CASE("resolve: identical mics without serial are never auto-assigned", "[devices][identity]")
{
    auto before = usbDev("ep-1", Flow::Capture, 0x0d8c, 0x0014, "7&abc&0&0001", false, "USB PnP Mic");
    auto saved = identityOf(before);
    auto other = usbDev("ep-2", Flow::Capture, 0x0d8c, 0x0014, "7&def&0&0002", false, "USB PnP Mic");
    auto r = resolve(saved, {other}, {});
    CHECK(r.kind == MatchKind::PossibleMatch);
    CHECK(r.endpointId == "ep-2");
}

TEST_CASE("resolve: an unplugged device keeps its channel OFFLINE even if a twin is present", "[devices][identity]")
{
    auto mine = usbDev("ep-1", Flow::Capture, 0x17a0, 0x0305, "SER1", true, "Yeti", false);
    auto twin = usbDev("ep-2", Flow::Capture, 0x17a0, 0x0305, "SER2", true, "Yeti", true);
    auto r = resolve(identityOf(mine), {mine, twin}, {});
    CHECK(r.kind == MatchKind::None);
}

TEST_CASE("resolve: never reuses an endpoint taken by another channel", "[devices][identity]")
{
    auto mic = usbDev("ep-A", Flow::Capture, 1, 2, "S", true, "M");
    CHECK(resolve(identityOf(mic), {mic}, {"ep-A"}).kind == MatchKind::None);
}

TEST_CASE("resolveAll keeps channels independent and exact matches first", "[devices][identity]")
{
    auto a = usbDev("ep-A", Flow::Capture, 1, 2, "SA", true, "Mic A");
    auto b = usbDev("ep-B", Flow::Capture, 1, 2, "SB", true, "Mic B");
    auto hpA = usbDev("hp-A", Flow::Render, 3, 4, "HA", true, "HP A");
    Assignments as = Assignments::defaults();
    as.ch[0].mic = identityOf(a);
    as.ch[1].mic = identityOf(b);
    as.ch[0].headphones = identityOf(hpA);
    as.ch[2].mic = identityOf(a); // duplicate assignment of the same endpoint + channel
    auto res = resolveAll(as, {a, b, hpA});
    CHECK(res[0].mic.kind == MatchKind::Exact);
    CHECK(res[1].mic.kind == MatchKind::Exact);
    CHECK(res[0].headphones.kind == MatchKind::Exact);
    CHECK(res[2].mic.kind != MatchKind::Exact);   // ch1 already owns ep-A input -1
    CHECK(res[3].mic.kind == MatchKind::None);

    // Unplug B: only channel 2 changes, nothing is reassigned.
    b.raw.state = DeviceState::Unplugged;
    auto res2 = resolveAll(as, {a, b, hpA});
    CHECK(res2[0].mic.endpointId == "ep-A");
    CHECK(res2[1].mic.kind == MatchKind::None);
    CHECK(res2[0].headphones.endpointId == "hp-A");
}

TEST_CASE("Assignments JSON round-trip", "[devices][identity]")
{
    Assignments as = Assignments::defaults();
    as.ch[0].mic = identityOf(usbDev("ep-A", Flow::Capture, 0x46d, 0x85e, "S1", true, "Mic \"A\""));
    as.ch[0].micChannel = 1;
    as.ch[4].headphones = identityOf(usbDev("hp-5", Flow::Render, 9, 9, "", false, "HP 5"));
    as.ch[4].hpPair = 2;
    as.ch[7].name = "Producer";
    as.outputs[static_cast<size_t>(OutputRole::Monitor)].device = identityOf(usbDev("mon", Flow::Render, 7, 7, "M1", true, "Monitors"));
    as.outputs[static_cast<size_t>(OutputRole::CleanStream)].pair = 1;
    as.preferredMaster = "hp-5";
    as.talkback.mic = identityOf(usbDev("prod", Flow::Capture, 8, 8, "P1", true, "Producer Mic"));
    as.talkback.micChannel = 1;
    auto back = assignmentsFromJson(toJson(as));
    REQUIRE(back.has_value());
    CHECK(*back == as);
    CHECK_FALSE(assignmentsFromJson("not json").has_value());
}
