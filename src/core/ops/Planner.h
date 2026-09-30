#pragma once
// ChangeSet → ordered ApplyPlan (ARCHITECTURE §2.3). Order: removals first (packages,
// capabilities, AppX, system components), then features, drivers, updates, and registry/services
// last (they may target files the earlier steps add or remove). Within a phase the user's order
// is kept — except the component store cleanup, which runs before everything: DISM refuses it
// once another step has left a pending operation in the image.
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

// Consecutive steps of one phase, as the Apply screens list them.
struct PlanGroup {
    Phase phase;
    std::size_t first = 0; // index into plan.steps
    std::size_t count = 0;
    double estimateSeconds = 0;
};
[[nodiscard]] std::vector<PlanGroup> groups(const ApplyPlan& plan);

// Rough per-operation duration on a mounted image (SSD); refined from real runs (ENGINE.md).
[[nodiscard]] double estimateSeconds(OpKind kind) noexcept;
// Commit + unmount of a Windows 11 install image (lab: ~90 s).
inline constexpr double kCommitSeconds = 90.0;

} // namespace wl::core::ops
