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
#include "core/image/dism/StoreShrink.h"
#include "core/image/AppxInstall.h"
#include "core/image/Branding.h"
#include "core/image/HostsFile.h"
#include "core/image/icons/IconPatch.h"
#include "core/image/LanguageInstall.h"
#include "core/image/ScheduledTasks.h"
#include "core/image/dism/DefaultApps.h"
#include "core/image/dism/DismExe.h"
#include "core/image/dism/WinReUpdate.h"
#include "core/image/dism/Intl.h"
#include "core/postsetup/PostSetup.h"
#include "core/system/Files.h"

#include <windows.h>

#include <algorithm>
#include <format>

namespace wl::core::ops {

namespace {

// RemoveAppx names a package by its full name, version included. A preset made on another build
// names another version of the same app: DISM answers "not found" and the app stayed while the
// report said done (audit A4). Removes what this image provisions for the app's family instead;
// only an app the image really does not have counts as removed.
Result<void> removeAppxFamily(DismSession& session, const std::wstring& fullName,
                              std::unique_ptr<OfflineRegistry>& registry, const TaskContext& task) {
    const std::wstring family = appxFamilyName(fullName);
    if (family.empty()) {
        log::info("apply", L"app not in the image: " + fullName);
        return {};
    }
    registry.reset(); // listing the apps loads the image's SOFTWARE hive in DISM
    auto provisioned = session.appxPackages();
    if (!provisioned) {
        return std::unexpected(provisioned.error());
    }
    Result<void> outcome;
    bool found = false;
    for (const auto& app : *provisioned) {
        if (_wcsicmp(appxFamilyName(app.packageName).c_str(), family.c_str()) != 0) {
            continue;
        }
        found = true;
        log::info("apply", L"another version of the app is in this image; removing it: " + app.packageName);
        auto removed = session.removeAppx(app.packageName);
        if (!removed && removed.error().hresult == kAppxRemovalRefused) {
            removed = removeAppxNative(session, app.packageName, TaskContext{task.cancel, {}});
        }
        if (!removed) {
            outcome = std::move(removed);
            break;
        }
    }
    if (!found) {
        log::info("apply", L"app not in the image (already removed or never there): " + fullName);
    }
    // The listing keeps SOFTWARE loaded by DISM until the session closes; later registry steps of
    // the run would fail with 0x80070020 (SystemComponents.cpp). A new session lets go of it.
    session.suspend();
    if (auto reopened = session.reload(); !reopened) {
        return reopened;
    }
    return outcome;
}

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
        // No package by this full name: ERROR_INSTALL_PACKAGE_NOT_FOUND (0x80073CF1, what DISM answers
        // on 26200 — lab_audit_apply, 2026-10-09) or ERROR_FILE_NOT_FOUND. Another version of the app
        // (a preset from another build) is removed by its family; an edition that never had it, or an
        // image the preset was already applied to, has nothing to remove — what "remove" asked for.
        if (!removed && (removed.error().hresult == static_cast<std::int32_t>(0x80073CF1) ||
                         removed.error().hresult == static_cast<std::int32_t>(0x80070002))) {
            return removeAppxFamily(session, op.target, registry, task);
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
    case OpKind::AddPackage:
        // A language file may need staging first: UUP names, the pack as an ESD (D-061).
        if (op.value == L"language") {
            return addLanguagePackage(session, op.target, task);
        }
        // D-080: the Safe OS dynamic update goes into the image's WinRE, not into the image.
        if (op.value == L"safeos") {
            auto dism = Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            const auto work = session.mountPath().parent_path() / (session.mountPath().filename().wstring() + L"-winre");
            auto updated = updateWinRe(**dism, session.mountPath(), work, {op.target, options.winReLcu}, task);
            if (!updated && updated.error().code == ErrorCode::NotFound) {
                log::info("apply", L"the image has no WinRE (removed): the Safe OS update has nothing to update");
                return {};
            }
            if (!updated) {
                return std::unexpected(updated.error());
            }
            return {};
        }
        return addPackageOrDismExe(session, op.target, task);
    case OpKind::AddDriver: return session.addDriver(op.target);
    case OpKind::SetServiceStart: {
        // The name comes from a preset file: it must stay one key under Services.
        if (!validServiceName(op.target)) {
            return fail(ErrorCode::InvalidArgument, L"bad service name", op.target);
        }
        const auto start = startTypeFromKey(op.value);
        if (!start) {
            return fail(ErrorCode::InvalidArgument, L"unknown service start type", op.value);
        }
        // A catalog setting may name a service this Windows does not have (WSAIFabricSvc before
        // 24H2): writing Start would leave a key without a service behind.
        if (auto exists = reg().keyExists(L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\" + op.target); exists && !*exists) {
            log::info("apply", L"service not in the image, skipped: " + op.target);
            return {};
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
    case OpKind::WriteFile: return writeImageFile(session.mountPath(), op.target, imageFileBytes(op.value));
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
    case OpKind::SetPicture: {
        const auto slot = pictureSlotFromKey(op.target);
        if (!slot) {
            return fail(ErrorCode::InvalidArgument, L"unknown picture", op.target);
        }
        auto written = applyPicture(session.mountPath(), *slot, op.value, task);
        if (!written) {
            return std::unexpected(written.error());
        }
        for (const auto& write : written->registry) {
            if (auto r = reg().apply(write); !r) {
                return r;
            }
        }
        return {};
    }
    case OpKind::PatchIcons: {
        auto request = iconPatchRequest(op.value);
        if (!request) {
            return std::unexpected(request.error());
        }
        return applyIconPatch(session.mountPath(), op.target, *request);
    }
    case OpKind::AddFont: {
        auto write = applyFont(session.mountPath(), op.value);
        if (!write) {
            return std::unexpected(write.error());
        }
        return reg().apply(*write);
    }
    case OpKind::SetDefaultApps: {
        registry.reset();
        return importAssociations(session, utf8::fromWide(op.value), tempFolder() / L"WinLove", task);
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
    case OpKind::ShrinkStore: {
        if (auto shrink = storeShrinkFromJson(utf8::fromWide(op.value)); !shrink) {
            return std::unexpected(shrink.error());
        }
        registry.reset();
        auto shrunk = shrinkComponentStore(session.mountPath(), task);
        if (!shrunk) {
            return std::unexpected(shrunk.error());
        }
        log::info("apply", std::format(L"WinSxS shrunk: {} folders removed, {} kept, {:.2f} GB freed", shrunk->removed.size(),
                                       shrunk->kept, static_cast<double>(shrunk->freed) / (1024.0 * 1024 * 1024)));
        return {};
    }
    }
    return fail(ErrorCode::Unsupported, L"operation kind not implemented yet", utf8::toWide(opKindKey(op.kind)));
}

} // namespace

std::size_t ApplyReport::failures() const {
    return static_cast<std::size_t>(std::ranges::count_if(results, [](const StepResult& r) { return !r.outcome; }));
}

ApplyReport apply(const ApplyPlan& plan, DismSession& session, const TaskContext& task, ErrorPolicy policy,
                  const ApplyCallbacks& callbacks, const ApplyOptions& given) {
    ApplyReport report;
    ApplyOptions options = given;
    if (options.winReLcu.empty()) {
        for (const auto& step : plan.steps) {
            if (step.operation.kind == OpKind::AddPackage && step.operation.value == L"lcu") {
                options.winReLcu = step.operation.target;
            }
        }
    }
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
