#pragma once
// ChangeSet → ordered ApplyPlan (ARCHITECTURE §2.3). Order: the edition change before anything
// else (DISM changes the edition of an image with nothing pending, and what follows then works on
// the edition that will be installed), then removals (packages,
// capabilities, AppX, system components), then features, drivers, updates, and registry/services
// last (they may target files the earlier steps add or remove). Within a phase the user's order
// is kept. The component store cleanup has a phase of its own right after the updates: that is
// when there is something to clean (what the updates superseded; on untouched Microsoft media it
// finds nothing), as in Microsoft's media refresh procedure. DISM refuses it (0x800F0806) when an
// earlier step left a pending operation — enabling a feature such as .NET 3.5 does.
#include "core/ops/ChangeSet.h"

#include <vector>

namespace wl::core::ops {

// Apps: provisioned .appx / .msix (D-050) — after the updates, whose servicing stack may be needed.
enum class Phase : std::uint8_t { Edition, Remove, Features, Drivers, Updates, Apps, Cleanup, Settings };

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
// The same for one step of a plan: the store cleanup takes seconds on an image nothing was added
// to (lab: 13 s) and many minutes after updates were integrated.
[[nodiscard]] double estimateSeconds(const ApplyPlan& plan, std::size_t step) noexcept;
// Commit + unmount of a Windows 11 install image (lab: ~90 s).
inline constexpr double kCommitSeconds = 90.0;
// Rewriting the WIM after the commit (WimGapi optimizeWim): a copy of its streams, no recompression.
inline constexpr double kOptimizeSeconds = 30.0; // lab: 17 s for a 6.8 GB, 6-edition install.wim

} // namespace wl::core::ops
