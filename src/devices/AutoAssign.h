#pragma once
// Auto Assign (DEVICE_MANAGEMENT.md §4): proposes device assignments for EMPTY cells only. It never
// proposes changing an assigned cell; the UI shows every proposed change and applies nothing
// without confirmation.
#include <optional>
#include <string>
#include <vector>

#include "devices/DeviceIdentity.h"

namespace pf8 {

struct AssignChange
{
    int channel = 0;
    bool mic = true; // false = headphones
    std::string endpointId;
    std::string deviceName;
};

struct AutoAssignProposal
{
    std::vector<AssignChange> changes;
    int emptyMicCells = 0, emptyHeadphoneCells = 0;
    bool empty() const noexcept { return changes.empty(); }
};

// Order: USB headsets (mic + headphones of the same container go to the same channel), then USB
// microphones, then USB headphones. Only online devices; built-in/HDMI/virtual devices are never
// auto-assigned. Endpoints already used by any channel are skipped.
AutoAssignProposal proposeAutoAssign(const Assignments& current, const std::vector<DeviceInfo>& devices);

// Applies a (confirmed) proposal: only cells that are still empty are filled.
Assignments applyProposal(const Assignments& current, const AutoAssignProposal& p, const std::vector<DeviceInfo>& devices);

} // namespace pf8
