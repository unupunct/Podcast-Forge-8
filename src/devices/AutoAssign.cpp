#include "devices/AutoAssign.h"

#include <algorithm>
#include <set>

namespace pf8 {
namespace {

bool isUsb(const DeviceInfo& d)
{
    switch (d.kind)
    {
        case DeviceKind::UsbMicrophone:
        case DeviceKind::UsbHeadset:
        case DeviceKind::UsbHeadphones:
        case DeviceKind::UsbInterface: return true;
        default: return false;
    }
}

const DeviceInfo* byId(const std::vector<DeviceInfo>& devices, const std::string& id)
{
    for (const auto& d : devices)
        if (d.raw.endpointId == id) return &d;
    return nullptr;
}

} // namespace

AutoAssignProposal proposeAutoAssign(const Assignments& current, const std::vector<DeviceInfo>& devices)
{
    AutoAssignProposal p;
    std::set<std::string> used;
    for (const auto& c : current.ch)
    {
        if (c.mic) used.insert(c.mic->endpointId);
        if (c.headphones) used.insert(c.headphones->endpointId);
    }
    for (const auto& o : current.outputs)
        if (o.device) used.insert(o.device->endpointId);

    std::vector<const DeviceInfo*> caps, rends;
    for (const auto& d : devices)
    {
        if (!d.online() || !isUsb(d) || used.count(d.raw.endpointId)) continue;
        (d.raw.flow == Flow::Capture ? caps : rends).push_back(&d);
    }
    auto order = [](const DeviceInfo* a, const DeviceInfo* b) {
        if (a->pairedInContainer != b->pairedInContainer) return a->pairedInContainer; // headsets first
        if (a->raw.friendlyName != b->raw.friendlyName) return a->raw.friendlyName < b->raw.friendlyName;
        return a->raw.endpointId < b->raw.endpointId;
    };
    std::sort(caps.begin(), caps.end(), order);
    std::sort(rends.begin(), rends.end(), order);

    bool micTaken[8]{}, hpTaken[8]{};
    for (size_t i = 0; i < 8; ++i)
    {
        micTaken[i] = current.ch[i].mic.has_value();
        hpTaken[i] = current.ch[i].headphones.has_value();
        if (!micTaken[i]) ++p.emptyMicCells;
        if (!hpTaken[i]) ++p.emptyHeadphoneCells;
    }
    std::set<std::string> proposed;
    auto propose = [&](int ch, bool mic, const DeviceInfo* d) {
        p.changes.push_back({ch, mic, d->raw.endpointId, d->raw.friendlyName});
        proposed.insert(d->raw.endpointId);
        (mic ? micTaken : hpTaken)[ch] = true;
    };

    // 1. Headsets: a channel with both cells empty gets the mic and headphones of one container.
    for (const auto* c : caps)
    {
        if (!c->pairedInContainer || proposed.count(c->raw.endpointId)) continue;
        const DeviceInfo* partner = nullptr;
        for (const auto* r : rends)
            if (!proposed.count(r->raw.endpointId) && !r->raw.containerId.empty() && r->raw.containerId == c->raw.containerId)
            {
                partner = r;
                break;
            }
        if (!partner) continue;
        for (int ch = 0; ch < 8; ++ch)
            if (!micTaken[ch] && !hpTaken[ch])
            {
                propose(ch, true, c);
                propose(ch, false, partner);
                break;
            }
    }
    // 2. Remaining microphones into empty mic cells, in channel order.
    for (const auto* c : caps)
    {
        if (proposed.count(c->raw.endpointId)) continue;
        for (int ch = 0; ch < 8; ++ch)
            if (!micTaken[ch])
            {
                propose(ch, true, c);
                break;
            }
    }
    // 3. Remaining headphones into empty headphone cells.
    for (const auto* r : rends)
    {
        if (proposed.count(r->raw.endpointId)) continue;
        for (int ch = 0; ch < 8; ++ch)
            if (!hpTaken[ch])
            {
                propose(ch, false, r);
                break;
            }
    }
    std::sort(p.changes.begin(), p.changes.end(), [](const AssignChange& a, const AssignChange& b) {
        return a.channel != b.channel ? a.channel < b.channel : a.mic > b.mic;
    });
    return p;
}

Assignments applyProposal(const Assignments& current, const AutoAssignProposal& p, const std::vector<DeviceInfo>& devices)
{
    Assignments a = current;
    for (const auto& c : p.changes)
    {
        auto& cell = c.mic ? a.ch[static_cast<size_t>(c.channel)].mic : a.ch[static_cast<size_t>(c.channel)].headphones;
        if (cell) continue; // something was assigned meanwhile: never overwrite
        if (const auto* d = byId(devices, c.endpointId)) cell = identityOf(*d);
    }
    return a;
}

} // namespace pf8
