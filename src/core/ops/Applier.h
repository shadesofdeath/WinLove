#pragma once
// Runs an ApplyPlan against a DISM servicing session, step by step, on the engine thread.
// Each step is logged and reported; on error the policy decides: stop, or skip and continue
// (interaction.md "Kuyruk modeli": "Yeniden dene" / "Atla ve devam").
// Operation kinds whose backend does not exist yet fail with ErrorCode::Unsupported.
#include "core/image/dism/Dism.h"
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
// disabling a feature does that).
[[nodiscard]] bool unchangedByFailure(const StepResult& result) noexcept;

struct ApplyOptions {
    std::vector<std::filesystem::path> featureSources; // DismEnableFeature SourcePaths (sources\sxs)
    // D-080: the cumulative update queued in the same run, whose servicing stack goes into WinRE
    // before the Safe OS dynamic update (apply() finds it in the plan when this is empty).
    std::filesystem::path winReLcu;
};

struct ApplyCallbacks {
    std::function<void(std::size_t index, const PlanStep& step)> stepStarted;
    std::function<void(std::size_t index, const StepResult& result)> stepFinished;
};

[[nodiscard]] ApplyReport apply(const ApplyPlan& plan, DismSession& session, const TaskContext& task,
                                ErrorPolicy policy = ErrorPolicy::Stop, const ApplyCallbacks& callbacks = {},
                                const ApplyOptions& options = {});

} // namespace wl::core::ops
