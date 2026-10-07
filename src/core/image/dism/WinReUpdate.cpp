#include "core/image/dism/WinReUpdate.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/image/ImageFiles.h"
#include "core/image/WimFile.h"
#include "core/image/dism/DismExe.h"
#include "core/image/wim/WimGapi.h"
#include "core/io/ByteSource.h"

#include <windows.h>

#include <format>

namespace wl::core {

namespace {

constexpr std::int32_t kKnownLcuFailure = static_cast<std::int32_t>(0x8007007E); // Microsoft: "a known issue … we can ignore"

std::wstring wimVersion(const std::filesystem::path& wim) {
    auto file = DiskFile::open(nativePath(wim));
    if (!file) {
        return {};
    }
    auto read = readWim(**file);
    return read && !read->images.empty() ? read->images.front().versionString() : std::wstring();
}

std::uint32_t bootIndexOf(const std::filesystem::path& wim) {
    auto file = DiskFile::open(nativePath(wim));
    if (!file) {
        return 0;
    }
    auto header = readWimHeader(**file);
    return header ? header->bootIndex : 0;
}

std::uint64_t sizeOf(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    return ec ? 0 : size;
}

// A stage of the whole run, 0 … 1.
TaskContext stage(const TaskContext& task, double from, double to, std::wstring label) {
    return TaskContext{task.cancel, [&task, from, to, label](double f, std::wstring_view) { task.report(from + (to - from) * f, label); }};
}

} // namespace

Result<WinReUpdateReport> updateWinRe(Dism& dism, const std::filesystem::path& mountDir, const std::filesystem::path& workDir,
                                      const WinReUpdate& update, const TaskContext& task) {
    const auto original = mountDir / kWinReRelative;
    if (!std::filesystem::exists(original)) {
        return fail(ErrorCode::NotFound, L"the image has no WinRE (Windows\\System32\\Recovery\\Winre.wim)", mountDir.wstring());
    }
    if (update.safeOs.empty() || !std::filesystem::exists(update.safeOs)) {
        return fail(ErrorCode::InvalidArgument, L"no Safe OS dynamic update to add", update.safeOs.wstring());
    }
    WinReUpdateReport report;
    std::error_code ec;
    std::filesystem::create_directories(workDir, ec);
    const auto copy = workDir / L"winre.wim";
    const auto exported = workDir / L"winre2.wim";
    const auto mount = workDir / L"mount";
    std::filesystem::remove(copy, ec);
    std::filesystem::remove(exported, ec);
    std::filesystem::create_directories(mount, ec);
    if (!CopyFileW(original.c_str(), copy.c_str(), FALSE)) {
        return fail(ErrorCode::IoError, L"cannot copy WinRE out of the image", original.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    SetFileAttributesW(copy.c_str(), FILE_ATTRIBUTE_NORMAL);
    report.versionBefore = wimVersion(copy);
    report.bytesBefore = sizeOf(copy);
    log::info("winre", std::format(L"updating WinRE {} ({} bytes)", report.versionBefore, report.bytesBefore));

    if (auto mounted = dism.mount(copy, 1, mount, /*readOnly=*/false, stage(task, 0.0, 0.1, L"WinRE")); !mounted) {
        return std::unexpected(mounted.error());
    }
    // From here on a failure unmounts without saving.
    auto serviced = [&]() -> Result<void> {
        auto session = dism.openSession(mount);
        if (!session) {
            return std::unexpected(session.error());
        }
        if (!update.lcu.empty()) {
            auto ssu = addPackageOrDismExe(**session, update.lcu, stage(task, 0.1, 0.45, L"WinRE"));
            if (!ssu && ssu.error().hresult != kKnownLcuFailure) {
                return ssu;
            }
            if (!ssu) {
                log::info("winre", L"cumulative update in WinRE: 0x8007007E, the known combined-update failure (ignored)");
            }
        }
        if (auto safeOs = addPackageOrDismExe(**session, update.safeOs, stage(task, 0.45, 0.65, L"WinRE")); !safeOs) {
            return safeOs;
        }
        auto cleaned = runDismExe(**session, L"/Cleanup-Image /StartComponentCleanup /ResetBase /Defer",
                                  [&](double f) { task.report(0.65 + 0.15 * f, L"WinRE"); });
        if (!cleaned) {
            return std::unexpected(cleaned.error());
        }
        if (cleaned->exitCode != 0) {
            return std::unexpected(dismExeFailure(*cleaned, L"WinRE cleanup"));
        }
        return {};
    }();
    if (!serviced) {
        (void)dism.unmount(mount, /*commit=*/false, TaskContext{task.cancel, {}});
        return std::unexpected(serviced.error());
    }
    if (auto saved = dism.unmount(mount, /*commit=*/true, stage(task, 0.8, 0.88, L"WinRE")); !saved) {
        (void)dism.unmount(mount, /*commit=*/false, TaskContext{task.cancel, {}});
        return std::unexpected(saved.error());
    }
    const std::uint32_t bootIndex = bootIndexOf(copy);
    if (auto out = exportImage(copy, 1, exported, WimCompression::Lzx, stage(task, 0.88, 0.95, L"WinRE")); !out) {
        return std::unexpected(out.error());
    }
    // An export does not carry the boot flag; the recovery boot entry starts the image it names.
    if (bootIndex != 0) {
        if (auto marked = setBootImage(exported, 1); !marked) {
            return std::unexpected(marked.error());
        }
    }
    log::info("winre", std::format(L"WinRE boot index: original {}, exported {}", bootIndex, bootIndexOf(exported)));
    report.versionAfter = wimVersion(exported);
    report.bytesAfter = sizeOf(exported);
    if (auto put = replaceImageFileFrom(mountDir, kWinReRelative, exported, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
                                        stage(task, 0.95, 1.0, L"WinRE"));
        !put) {
        return std::unexpected(put.error());
    }
    std::filesystem::remove(copy, ec);
    std::filesystem::remove(exported, ec);
    std::filesystem::remove(mount, ec);
    log::info("winre", std::format(L"WinRE {} → {} ({} → {} bytes)", report.versionBefore, report.versionAfter, report.bytesBefore,
                                   report.bytesAfter));
    return report;
}

} // namespace wl::core
