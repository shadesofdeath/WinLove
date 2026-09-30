#include "core/image/BootImage.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/image/RegistryEdit.h"
#include "core/image/WimFile.h"
#include "core/image/dism/MountHealth.h"
#include "core/io/ByteSource.h"

#include <format>

namespace wl::core {

namespace {

constexpr const wchar_t* kLabConfigKey = L"HKLM\\SYSTEM\\Setup\\LabConfig";

// What is done to the mounted image; the caller unmounts.
Result<void> applyPatch(Dism& dism, const std::filesystem::path& mountDir, const BootPatch& patch,
                        BootPatchReport& report, const TaskContext& task) {
    {
        // Hives first, with no DISM session open: a session keeps them to itself.
        OfflineRegistry registry(mountDir);
        for (const auto& value : patch.labConfigValues()) {
            auto write = parseRegValue(kLabConfigKey, value, L"dword:00000001");
            if (!write) {
                return std::unexpected(write.error());
            }
            if (auto written = registry.apply(*write); !written) {
                return written;
            }
            log::info("boot", L"LabConfig " + value + L" = 1");
        }
    } // unloaded here
    if (patch.drivers.empty()) {
        return {};
    }
    auto session = dism.openSession(mountDir);
    if (!session) {
        return std::unexpected(session.error());
    }
    for (std::size_t i = 0; i < patch.drivers.size(); ++i) {
        if (auto go = task.cancel.check(L"boot image drivers"); !go) {
            return go;
        }
        const auto& inf = patch.drivers[i];
        if (auto added = (*session)->addDriver(nativePath(inf)); added) {
            ++report.driversAdded;
            log::info("boot", L"driver added: " + inf.wstring());
        } else {
            // One driver Setup's image does not take must not cost the others (or the bypass).
            log::warn("boot", L"driver not added: " + describe(added.error()));
            report.driversRefused.push_back(inf.filename().wstring() + L": " + added.error().message);
        }
        task.report(static_cast<double>(i + 1) / static_cast<double>(patch.drivers.size()), L"drivers");
    }
    return {}; // the session closes here: DISM refuses to unmount with one open
}

} // namespace

std::vector<std::wstring> BootPatch::labConfigValues() const {
    std::vector<std::wstring> values;
    for (const auto& [on, name] : {std::pair{bypassTpm, L"BypassTPMCheck"},
                                   std::pair{bypassSecureBoot, L"BypassSecureBootCheck"},
                                   std::pair{bypassRam, L"BypassRAMCheck"},
                                   std::pair{bypassCpu, L"BypassCPUCheck"},
                                   std::pair{bypassStorage, L"BypassStorageCheck"}}) {
        if (on) {
            values.emplace_back(name);
        }
    }
    return values;
}

Result<int> setupImageIndex(const std::filesystem::path& bootWim) {
    auto file = DiskFile::open(nativePath(bootWim));
    if (!file) {
        return std::unexpected(file.error());
    }
    auto wim = readWim(**file);
    if (!wim) {
        return std::unexpected(wim.error());
    }
    if (wim->images.empty()) {
        return fail(ErrorCode::ParseError, L"the boot image has no editions", bootWim.wstring());
    }
    const int boot = static_cast<int>(wim->header.bootIndex);
    return boot > 0 ? boot : wim->images.back().index;
}

Result<BootPatchReport> patchBootImage(Dism& dism, const std::filesystem::path& bootWimInput,
                                       const std::filesystem::path& mountDir, const BootPatch& patch,
                                       const TaskContext& task) {
    BootPatchReport report;
    if (patch.empty()) {
        return report;
    }
    const std::filesystem::path bootWim = nativePath(bootWimInput);
    auto index = setupImageIndex(bootWim);
    if (!index) {
        return std::unexpected(index.error());
    }
    report.index = *index;
    log::info("boot", std::format(L"patch {} [{}]: {} LabConfig value(s), {} driver(s)", bootWim.wstring(), *index,
                                  patch.labConfigValues().size(), patch.drivers.size()));

    // Shares of the run: mount 35 %, the patch 15 %, commit 50 %.
    const TaskContext mountTask{task.cancel, [&](double f, std::wstring_view s) { task.report(0.35 * f, s); }};
    if (auto mounted = mountSafely(dism, bootWim, *index, mountDir, /*readOnly=*/false, mountTask); !mounted) {
        return std::unexpected(mounted.error());
    }
    const TaskContext patchTask{task.cancel, [&](double f, std::wstring_view s) { task.report(0.35 + 0.15 * f, s); }};
    const auto applied = applyPatch(dism, mountDir, patch, report, patchTask);

    // Not cancellable from here: a half-unmounted boot image helps nobody.
    const TaskContext unmountTask{CancelToken{}, [&](double f, std::wstring_view s) { task.report(0.5 + 0.5 * f, s); }};
    if (!applied) {
        log::error("boot", L"patch failed, discarding: " + describe(applied.error()));
        if (auto discarded = unmountSafely(dism, mountDir, /*commit=*/false, unmountTask); !discarded) {
            log::error("boot", L"and the boot image could not be unmounted: " + describe(discarded.error()));
        }
        return std::unexpected(applied.error());
    }
    if (auto saved = unmountSafely(dism, mountDir, /*commit=*/true, unmountTask); !saved) {
        return std::unexpected(saved.error());
    }
    task.report(1.0, L"boot image");
    return report;
}

} // namespace wl::core
