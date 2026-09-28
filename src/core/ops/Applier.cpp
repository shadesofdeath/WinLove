#include "core/ops/Applier.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <algorithm>
#include <format>

namespace wl::core::ops {

namespace {

Result<void> runStep(const Operation& op, DismSession& session, const TaskContext& task, const ApplyOptions& options) {
    switch (op.kind) {
    case OpKind::DisableFeature: return session.disableFeature(op.target, task);
    case OpKind::EnableFeature: return session.enableFeature(op.target, task, options.featureSources);
    case OpKind::RemovePackage: return session.removePackage(op.target, task);
    case OpKind::RemoveCapability: return session.removeCapability(op.target, task);
    case OpKind::RemoveAppx:
    case OpKind::AddDriver:
    case OpKind::AddPackage:
    case OpKind::SetRegistryValue:
    case OpKind::SetServiceStart: break;
    }
    return fail(ErrorCode::Unsupported, L"operation kind not implemented yet", utf8::toWide(opKindKey(op.kind)));
}

} // namespace

std::size_t ApplyReport::failures() const {
    return static_cast<std::size_t>(std::ranges::count_if(results, [](const StepResult& r) { return !r.outcome; }));
}

ApplyReport apply(const ApplyPlan& plan, DismSession& session, const TaskContext& task, ErrorPolicy policy,
                  const ApplyCallbacks& callbacks, const ApplyOptions& options) {
    ApplyReport report;
    const std::size_t total = plan.steps.size();
    for (std::size_t i = 0; i < total; ++i) {
        const PlanStep& step = plan.steps[i];
        if (auto cancelled = task.cancel.check(L"apply"); !cancelled) {
            log::warn("apply", std::format(L"cancelled before step {}/{}", i + 1, total));
            return report; // completed stays false; the image stays mounted (errors.cancelled)
        }
        if (callbacks.stepStarted) {
            callbacks.stepStarted(i, step);
        }
        log::info("apply", std::format(L"[{}/{}] {} {}", i + 1, total, utf8::toWide(opKindKey(step.operation.kind)),
                                       step.operation.target));
        // Overall progress: finished steps + this step's own fraction.
        const TaskContext stepTask{task.cancel, [&](double fraction, std::wstring_view stage) {
                                       task.report((static_cast<double>(i) + std::clamp(fraction, 0.0, 1.0)) /
                                                       static_cast<double>(total),
                                                   stage);
                                   }};
        StepResult result{step, runStep(step.operation, session, stepTask, options)};
        if (!result.outcome) {
            log::error("apply", describe(result.outcome.error()));
        }
        const bool failed = !result.outcome;
        report.results.push_back(std::move(result));
        if (callbacks.stepFinished) {
            callbacks.stepFinished(i, report.results.back());
        }
        if (failed && policy == ErrorPolicy::Stop) {
            return report;
        }
    }
    report.completed = true;
    task.report(1.0, L"done");
    return report;
}

} // namespace wl::core::ops
