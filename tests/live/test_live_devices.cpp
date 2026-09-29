#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <objbase.h>

#include <cstdio>

#include "devices/DeviceInfo.h"
#include "devices/WinEndpointEnumerator.h"

namespace {
struct ComScope
{
    ComScope() { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { CoUninitialize(); }
};
} // namespace

TEST_CASE("Live: endpoint enumeration returns well-formed endpoints", "[live][devices]")
{
    ComScope com;
    auto eps = pf8::enumerateEndpoints();
    if (eps.empty()) SKIP("no audio endpoints on this machine");

    int active = 0, usbParsed = 0;
    for (const auto& e : eps)
    {
        CHECK(e.endpointId.rfind("{0.0.", 0) == 0);
        if (e.state == pf8::DeviceState::Active)
        {
            ++active;
            CHECK(e.mixRate > 0);
            CHECK(e.mixChannels > 0);
        }
        if (e.parentInterfacePath.find("usb#") != std::string::npos ||
            e.parentInterfacePath.find("USB#") != std::string::npos)
        {
            CHECK(pf8::parseUsbInterfacePath(e.parentInterfacePath).has_value());
            ++usbParsed;
        }
    }
    CHECK(active > 0);

    for (const auto& d : pf8::classify(eps))
        if (d.online())
            std::printf("  %-7s %-22s %-55s %s rates=%zu\n", pf8::toString(d.raw.flow), pf8::toString(d.kind),
                        d.raw.friendlyName.c_str(), d.raw.enumerator.c_str(), d.raw.exclusiveRates.size());
}
