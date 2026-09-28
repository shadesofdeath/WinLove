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
};

struct ApplyOptions {
    std::vector<std::filesystem::path> featureSources; // DismEnableFeature SourcePaths (sources\sxs)
};

struct ApplyCallbacks {
    std::function<void(std::size_t index, const PlanStep& step)> stepStarted;
    std::function<void(std::size_t index, const StepResult& result)> stepFinished;
};

[[nodiscard]] ApplyReport apply(const ApplyPlan& plan, DismSession& session, const TaskContext& task,
                                ErrorPolicy policy = ErrorPolicy::Stop, const ApplyCallbacks& callbacks = {},
                                const ApplyOptions& options = {});

} // namespace wl::core::ops
