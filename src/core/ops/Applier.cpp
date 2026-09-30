#include "core/ops/Applier.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/RegistryEdit.h"
#include "core/image/Services.h"
#include "core/image/SystemComponents.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/postsetup/PostSetup.h"

#include <algorithm>
#include <format>

namespace wl::core::ops {

namespace {

Result<void> runStep(const Operation& op, DismSession& session, const TaskContext& task, const ApplyOptions& options,
                     std::unique_ptr<OfflineRegistry>& registry, std::unique_ptr<DeferredRegistry>& deferred) {
    auto reg = [&]() -> OfflineRegistry& {
        if (!registry) {
            registry = std::make_unique<OfflineRegistry>(session.mountPath());
        }
        return *registry;
    };
    switch (op.kind) {
    case OpKind::DisableFeature: return session.disableFeature(op.target, task);
    case OpKind::EnableFeature: return session.enableFeature(op.target, task, options.featureSources);
    case OpKind::RemovePackage: return session.removePackage(op.target, task);
    case OpKind::RemoveCapability: return session.removeCapability(op.target, task);
    case OpKind::RemoveAppx: return session.removeAppx(op.target);
    case OpKind::AddPackage: return session.addPackage(op.target, task);
    case OpKind::AddDriver: return session.addDriver(op.target);
    case OpKind::SetServiceStart: {
        const auto start = startTypeFromKey(op.value);
        if (!start) {
            return fail(ErrorCode::InvalidArgument, L"unknown service start type", op.value);
        }
        for (const auto& write : serviceStartWrites(op.target, *start)) {
            if (auto r = reg().apply(write); !r) {
                return r;
            }
        }
        return {};
    }
    case OpKind::SetRegistryValue: {
        auto write = registryWriteFrom(op.target, op.value);
        if (!write) {
            return std::unexpected(write.error());
        }
        return reg().apply(*write);
    }
    case OpKind::SetRegistryFirstLogon: {
        auto write = registryWriteFrom(op.target, op.value);
        if (!write) {
            return std::unexpected(write.error());
        }
        if (!deferred) {
            deferred = std::make_unique<DeferredRegistry>(session.mountPath());
        }
        return deferRegistryWrite(reg(), *deferred, *write);
    }
    case OpKind::SetPostSetup: {
        auto plan = postSetupFromJson(utf8::fromWide(op.value));
        if (!plan) {
            return std::unexpected(plan.error());
        }
        return applyPostSetup(session.mountPath(), *plan, task);
    }
    case OpKind::RemoveComponent: {
        auto recipe = componentRecipeFromJson(utf8::fromWide(op.value));
        if (!recipe) {
            return std::unexpected(recipe.error());
        }
        registry.reset(); // the recipe loads the hives itself, with the DISM session closed
        return removeComponent(session, *recipe, task);
    }
    case OpKind::CleanupImage: {
        auto cleanup = storeCleanupFromJson(utf8::fromWide(op.value));
        if (!cleanup) {
            return std::unexpected(cleanup.error());
        }
        registry.reset(); // dism.exe loads the image's hives
        return cleanupComponentStore(session, cleanup->resetBase, task);
    }
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
    std::unique_ptr<OfflineRegistry> registry; // hives stay loaded for the run, unloaded on return
    std::unique_ptr<DeferredRegistry> deferred; // the post-setup .reg files, tidied on return
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
        StepResult result{step, runStep(step.operation, session, stepTask, options, registry, deferred)};
        if (!result.outcome) {
            log::error("apply", describe(result.outcome.error()));
        }
        if (session.reloadRequired()) {
            log::info("apply", L"DISM asked for a new session (servicing stack changed); reopening");
            if (auto reloaded = session.reload(); !reloaded) {
                log::error("apply", describe(reloaded.error()));
                report.results.push_back(std::move(result));
                return report; // cannot continue without a session
            }
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
    // A step aborted by the cancel event fails like any other under Skip: never call that complete
    // (ApplyJob would go on to commit).
    report.completed = !task.cancel.cancelled();
    if (!report.completed) {
        log::warn("apply", L"cancelled during the last step");
        return report;
    }
    task.report(1.0, L"done");
    return report;
}

} // namespace wl::core::ops
