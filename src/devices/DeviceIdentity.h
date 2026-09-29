#pragma once
// Stable device identity and the rules that restore a saved assignment (DEVICE_MANAGEMENT.md §2).
#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "devices/DeviceInfo.h"

namespace pf8 {

struct DeviceIdentity
{
    std::string endpointId;   // primary key
    Flow flow = Flow::Render;
    std::string containerId;
    uint16_t vid = 0, pid = 0;
    std::string serial;       // empty when the device reports no serial
    std::string friendlyName;
    bool operator==(const DeviceIdentity&) const = default;
};

DeviceIdentity identityOf(const DeviceInfo& d);

enum class MatchKind { Exact, Fingerprint, PossibleMatch, None };
const char* toString(MatchKind k) noexcept;

struct Resolution
{
    MatchKind kind = MatchKind::None;
    std::string endpointId; // the resolved endpoint (Exact/Fingerprint) or the candidate (PossibleMatch)
};

// `devices`: current registry contents (online and offline). `taken`: endpoints already resolved for
// other channels in the same role — never reused.
Resolution resolve(const DeviceIdentity& saved, const std::vector<DeviceInfo>& devices,
                   const std::set<std::string>& taken);

struct ChannelAssignment
{
    std::string name;
    std::optional<DeviceIdentity> mic;
    int micChannel = -1; // -1 = average of all device channels
    std::optional<DeviceIdentity> headphones;
    int hpPair = 0;
    bool operator==(const ChannelAssignment&) const = default;
};

struct Assignments
{
    std::array<ChannelAssignment, 8> ch;
    std::string preferredMaster; // endpoint id, empty = automatic
    bool operator==(const Assignments&) const = default;

    static Assignments defaults();
};

std::string toJson(const Assignments& a);
std::optional<Assignments> assignmentsFromJson(const std::string& json);

// Resolution of every channel against the registry.
struct ChannelResolution
{
    Resolution mic;
    Resolution headphones;
};
std::array<ChannelResolution, 8> resolveAll(const Assignments& a, const std::vector<DeviceInfo>& devices);

} // namespace pf8
