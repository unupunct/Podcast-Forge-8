#include "engine/MasterClock.h"

#include <algorithm>

namespace pf8 {

std::optional<std::string> selectMaster(const std::vector<MasterCandidate>& candidates)
{
    std::vector<const MasterCandidate*> online;
    for (const auto& c : candidates)
        if (c.online) online.push_back(&c);
    if (online.empty()) return std::nullopt;

    std::stable_sort(online.begin(), online.end(),
                     [](const MasterCandidate* a, const MasterCandidate* b) { return a->order < b->order; });

    for (const auto* c : online)
        if (c->userPreferred) return c->endpointId;
    for (const auto* c : online)
        if (c->isHeadphone && c->flow == Flow::Render) return c->endpointId;
    for (const auto* c : online)
        if (c->flow == Flow::Render) return c->endpointId;
    return online.front()->endpointId;
}

} // namespace pf8
