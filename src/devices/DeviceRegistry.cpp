#include "devices/DeviceRegistry.h"

#include <map>

namespace pf8 {

RegistryDiff DeviceRegistry::rebuild(const std::vector<EndpointRaw>& snapshot)
{
    auto next = classify(snapshot);

    std::lock_guard lock(mutex_);
    std::map<std::string, const DeviceInfo*> old;
    for (const auto& d : devices_) old[d.raw.endpointId] = &d;

    RegistryDiff diff;
    for (const auto& d : next)
    {
        auto it = old.find(d.raw.endpointId);
        if (it == old.end())
            diff.added.push_back(d.raw.endpointId);
        else
        {
            if (!(it->second->raw == d.raw)) diff.changed.push_back(d.raw.endpointId);
            old.erase(it);
        }
    }
    for (const auto& [id, _] : old) diff.removed.push_back(id);

    devices_ = std::move(next);
    if (!diff.empty()) ++generation_;
    return diff;
}

std::vector<DeviceInfo> DeviceRegistry::devices() const
{
    std::lock_guard lock(mutex_);
    return devices_;
}

std::optional<DeviceInfo> DeviceRegistry::find(const std::string& endpointId) const
{
    std::lock_guard lock(mutex_);
    for (const auto& d : devices_)
        if (d.raw.endpointId == endpointId) return d;
    return std::nullopt;
}

uint64_t DeviceRegistry::generation() const
{
    std::lock_guard lock(mutex_);
    return generation_;
}

} // namespace pf8
