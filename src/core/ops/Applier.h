#pragma once
// Runs an ApplyPlan against a DISM servicing session, step by step, on the engine thread.
// Each step is logged and reported; on error the policy decides: stop, or skip and continue
// (interaction.md "Kuyruk modeli": "Yeniden dene" / "Atla ve devam").
// Operation kinds whose backend does not exist yet fail with ErrorCode::Unsupported.
#include "core/image/dism/Dism.h"
#include "core/image/dism/StoreShrink.h"
#include "core/ops/Planner.h"

#include <functional>

namespace wl::core::ops {

enum class ErrorPolicy : std::uint8_t { Stop, Skip };

struct StepResult {
    PlanStep step;
    Result<void> outcome;
};

struct ApplyReport {
    std::vector<StepResult> results;
    bool completed = false; // every step ran (some may have failed under Skip)
    [[nodiscard]] std::size_t failures() const;
    // The failures that may have left the image half changed, the ones a save waits for: all but
    // those that changed nothing (unchangedByFailure).
    [[nodiscard]] std::size_t holdingFailures() const;
};

// A failed step that left the image as it was: the component store cleanup DISM refuses while an
// earlier step of the run has an operation pending until first boot (0x800F0806; enabling or
// disabling a feature does that), or a capability removal DISM refuses because its package is
// permanent (0x800F0825, before anything is staged), or a package operation on an image that takes
// no more servicing (0x800F0830 — DISM's own refusal, or WinLove's before calling it).
[[nodiscard]] bool unchangedByFailure(const StepResult& result) noexcept;

// Steps that are CBS servicing as a whole (features, packages, capabilities, the edition, the store
// cleanup): none of them may run on an image whose servicing is closed (StoreShrink.h).
[[nodiscard]] bool usesServicing(const Operation& op) noexcept;
inline constexpr std::int32_t kServicingClosedCode = static_cast<std::int32_t>(0x800F0830); // CBS_E_IMAGE_UNSERVICEABLE

struct ApplyOptions {
    std::vector<std::filesystem::path> featureSources; // DismEnableFeature SourcePaths (sources\sxs)
    // D-080: the cumulative update queued in the same run, whose servicing stack goes into WinRE
    // before the Safe OS dynamic update (apply() finds it in the plan when this is empty).
    std::filesystem::path winReLcu;
    // Read before the run (ApplyJob, servicingClosed()): when closed, servicing steps are skipped
    // with kServicingClosedCode and a component keeps its packages (its files, registry and
    // services still go); a ShrinkStore on a shrunk store has nothing left to do.
    ServicingClosed servicingClosed = ServicingClosed::No;
    // D-125: at the end, the update guard for what the steps that ran asked (UpdateGuard.h).
    bool updateGuard = false;
};

struct ApplyCallbacks {
    std::function<void(std::size_t index, const PlanStep& step)> stepStarted;
    std::function<void(std::size_t index, const StepResult& result)> stepFinished;
};

[[nodiscard]] ApplyReport apply(const ApplyPlan& plan, DismSession& session, const TaskContext& task,
                                ErrorPolicy policy = ErrorPolicy::Stop, const ApplyCallbacks& callbacks = {},
                                const ApplyOptions& options = {});

} // namespace wl::core::ops
