#pragma once
// ChangeSet → ordered ApplyPlan (ARCHITECTURE §2.3). Order: removals first (packages,
// capabilities, AppX), then features, drivers, updates, and registry/services last (they may
// target files the earlier steps add or remove). Within a phase the user's order is kept.
#include "core/ops/ChangeSet.h"

#include <vector>

namespace wl::core::ops {

enum class Phase : std::uint8_t { Remove, Features, Drivers, Updates, Settings };

struct PlanStep {
    Phase phase;
    Operation operation;
};

struct ApplyPlan {
    std::vector<PlanStep> steps;
    std::vector<std::wstring> warnings; // shown on the Apply summary (P05)
    [[nodiscard]] bool hasHighRisk() const;
};

[[nodiscard]] Phase phaseOf(OpKind kind) noexcept;
[[nodiscard]] ApplyPlan plan(const ChangeSet& changes);

} // namespace wl::core::ops
