#pragma once
// The current set of known endpoints. Rebuilt on the device-control thread; read (by copy) from
// the UI. Never used from the audio thread.
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "devices/DeviceInfo.h"

namespace pf8 {

struct RegistryDiff
{
    std::vector<std::string> added;   // endpoint ids
    std::vector<std::string> removed; // no longer enumerated at all
    std::vector<std::string> changed; // state or properties changed
    bool empty() const noexcept { return added.empty() && removed.empty() && changed.empty(); }
};

class DeviceRegistry
{
public:
    RegistryDiff rebuild(const std::vector<EndpointRaw>& snapshot);

    std::vector<DeviceInfo> devices() const;
    std::optional<DeviceInfo> find(const std::string& endpointId) const;
    uint64_t generation() const;

private:
    mutable std::mutex mutex_;
    std::vector<DeviceInfo> devices_;
    uint64_t generation_ = 0;
};

} // namespace pf8
