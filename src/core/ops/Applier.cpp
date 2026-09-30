#include "core/ops/Applier.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/RegistryEdit.h"
#include "core/image/Services.h"
#include "core/image/ImageFiles.h"
#include "core/image/SystemComponents.h"
#include "core/image/dism/Appx.h"
#include "core/image/dism/Edition.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/image/AppxInstall.h"
#include "core/image/HostsFile.h"
#include "core/image/ScheduledTasks.h"
#include "core/image/dism/DefaultApps.h"
#include "core/image/dism/Intl.h"
#include "core/postsetup/PostSetup.h"

#include <windows.h>

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
    case OpKind::DisableFeature: {
        auto disabled = session.disableFeature(op.target, task);
        // CBS_E_UNKNOWN_UPDATE: the feature no longer exists — an earlier step removed the package
        // that carried it (the Media Player capability takes the "WindowsMediaPlayer" feature with
        // it). Gone is what "off" asked for.
        if (!disabled && disabled.error().hresult == static_cast<std::int32_t>(0x800F080C)) {
            log::info("apply", L"feature already gone with its package: " + op.target);
            return {};
        }
        return disabled;
    }
    case OpKind::EnableFeature: return session.enableFeature(op.target, task, options.featureSources);
    case OpKind::RemovePackage: return session.removePackage(op.target, task);
    case OpKind::RemoveCapability: return session.removeCapability(op.target, task);
    case OpKind::RemoveAppx: {
        auto removed = session.removeAppx(op.target);
        // ERROR_FILE_NOT_FOUND: the package is not provisioned in this image — an edition that
        // never had it, or a preset applied to an image it was already applied to. Absent is
        // what "remove" asked for.
        if (!removed && removed.error().hresult == static_cast<std::int32_t>(0x80070002)) {
            log::info("apply", L"app not in the image (already removed): " + op.target);
            return {};
        }
        // 0x80073CFA: DISM will not deprovision this app (Windows Security UI, App Installer).
        // WinLove then changes in the image what DISM changes for the apps it lets go (D-038).
        if (!removed && removed.error().hresult == kAppxRemovalRefused) {
            log::info("apply", L"DISM refuses this app; removing it with WinLove's own removal: " + op.target);
            registry.reset(); // the recipe loads the hives itself, with the DISM session closed
            return removeAppxNative(session, op.target, task);
        }
        return removed;
    }
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
    case OpKind::WriteFile: return writeImageFile(session.mountPath(), op.target, utf8::fromWide(op.value));
    case OpKind::CopyFile: return copyImageFile(session.mountPath(), op.target, op.value);
    case OpKind::SetEdition: {
        registry.reset(); // dism.exe loads the image's hives
        auto changed = setEdition(session, op.value, task);
        if (!changed) {
            // DISM refuses the edition an image already has ("cannot upgrade to the edition
            // specified"): a preset applied to an image it was applied to. That is what was asked.
            if (const auto now = readEditions(session); now && now->current == op.value) {
                log::info("apply", L"the image already is this edition: " + op.value);
                return {};
            }
        }
        return changed;
    }
    case OpKind::SetTaskState: return setTaskDisabled(session.mountPath(), op.target, op.value != L"enabled");
    case OpKind::SetHosts: return applyHostsSection(session.mountPath(), op.target, op.value);
    case OpKind::SetDns:
        // DNS is a set of registry values of the settings catalog (D-049); the kind is kept
        // for presets written by hand, which have nothing more to say.
        return fail(ErrorCode::Unsupported, L"DNS is set through the settings (registry) now", op.target);
    case OpKind::CopyTree: {
        auto copied = copyImageTree(session.mountPath(), op.target, op.value, task);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        return {};
    }
    case OpKind::RemoveDriver: {
        auto removed = session.removeDriver(op.target);
        // Not in the driver store (a preset applied twice, another edition): gone is what was asked.
        if (!removed && removed.error().hresult == static_cast<std::int32_t>(0x80070002)) {
            log::info("apply", L"driver not in the image (already removed): " + op.target);
            return {};
        }
        return removed;
    }
    case OpKind::AddAppx: {
        auto install = appxInstallFromJson(op.target, utf8::fromWide(op.value));
        if (!install) {
            return std::unexpected(install.error());
        }
        registry.reset(); // dism.exe loads the image's hives
        return provisionAppx(session, *install, task);
    }
    case OpKind::SetDefaultApps: {
        registry.reset();
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        return importAssociations(session, utf8::fromWide(op.value), std::filesystem::path(temp) / L"WinLove", task);
    }
    case OpKind::SetIntl: {
        auto intl = intlFromJson(utf8::fromWide(op.value));
        if (!intl) {
            return std::unexpected(intl.error());
        }
        registry.reset();
        return setIntl(session, *intl, task);
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
