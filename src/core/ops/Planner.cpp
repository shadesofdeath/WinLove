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

double estimateSeconds(OpKind kind) noexcept {
    switch (kind) {
    case OpKind::RemovePackage: return 8.0;
    case OpKind::RemoveCapability: return 15.0;
    case OpKind::RemoveAppx: return 3.0;
    case OpKind::DisableFeature: return 8.0;
    case OpKind::EnableFeature: return 20.0;
    case OpKind::AddDriver: return 6.0;
    case OpKind::AddPackage: return 60.0;
    case OpKind::SetRegistryValue: return 0.3;
    case OpKind::SetServiceStart: return 1.0;
    }
    return 5.0;
}

std::vector<PlanGroup> groups(const ApplyPlan& plan) {
    std::vector<PlanGroup> result;
    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        const auto& step = plan.steps[i];
        if (result.empty() || result.back().phase != step.phase) {
            result.push_back({step.phase, i, 0, 0});
        }
        ++result.back().count;
        result.back().estimateSeconds += estimateSeconds(step.operation.kind);
    }
    return result;
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
