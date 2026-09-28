#include "core/ops/Planner.h"

#include <algorithm>
#include <format>

namespace wl::core::ops {

Phase phaseOf(OpKind kind) noexcept {
    switch (kind) {
    case OpKind::RemovePackage:
    case OpKind::RemoveCapability:
    case OpKind::RemoveAppx: return Phase::Remove;
    case OpKind::DisableFeature:
    case OpKind::EnableFeature: return Phase::Features;
    case OpKind::AddDriver: return Phase::Drivers;
    case OpKind::AddPackage: return Phase::Updates;
    case OpKind::SetRegistryValue:
    case OpKind::SetServiceStart: return Phase::Settings;
    }
    return Phase::Settings;
}

bool ApplyPlan::hasHighRisk() const {
    return std::ranges::any_of(steps, [](const PlanStep& s) { return s.operation.risk == Risk::High; });
}

ApplyPlan plan(const ChangeSet& changes) {
    ApplyPlan result;
    for (const auto& op : changes.operations()) {
        result.steps.push_back({phaseOf(op.kind), op});
    }
    // stable: keeps the user's order inside each phase
    std::ranges::stable_sort(result.steps, {}, [](const PlanStep& s) { return static_cast<int>(s.phase); });

    const auto highRisk = std::ranges::count_if(result.steps, [](const PlanStep& s) { return s.operation.risk == Risk::High; });
    if (highRisk > 0) {
        result.warnings.push_back(std::format(L"{} high-risk operation(s): cannot be undone in the image", highRisk));
    }
    return result;
}

} // namespace wl::core::ops
