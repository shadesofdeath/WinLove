#include "core/image/BootImage.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/image/RegistryEdit.h"
#include "core/image/WimFile.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/wim/WimGapi.h"
#include "core/image/dism/DismExe.h"
#include "core/image/wim/WimVerify.h"
#include "core/io/ByteSource.h"

#include <format>

namespace wl::core {

namespace {

constexpr const wchar_t* kSetupKey = L"HKLM\\SYSTEM\\Setup";
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
        if (patch.legacySetup) {
            // Without it the command line would start nothing: the image is not a Setup one.
            if (!std::filesystem::exists(mountDir / L"sources" / L"setup.exe")) {
                return fail(ErrorCode::NotFound, L"the boot image has no sources\\setup.exe (not a Windows Setup image)",
                            mountDir.wstring());
            }
            std::wstring text = L"\"";
            for (const wchar_t c : kLegacySetupCmdLine) {
                text += c == L'\\' ? std::wstring(L"\\\\") : std::wstring(1, c);
            }
            auto write = parseRegValue(kSetupKey, L"CmdLine", text + L"\"");
            if (!write) {
                return std::unexpected(write.error());
            }
            if (auto written = registry.apply(*write); !written) {
                return written;
            }
            log::info("boot", L"Setup CmdLine = " + std::wstring(kLegacySetupCmdLine) + L" (previous Setup)");
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

Result<std::vector<int>> editionsWithoutWinre(const SourceInfo& source) {
    std::vector<int> missing;
    std::shared_ptr<const ByteSource> bytes;
    for (const auto& image : source.install.images) {
        if (image.build < 26100) {
            continue; // before 24H2 there is only the previous Setup
        }
        if (!bytes) {
            auto opened = openInstallImage(source);
            if (!opened) {
                return std::unexpected(opened.error());
            }
            bytes = *opened;
        }
        auto found = wimFileExists(*bytes, image.index, L"Windows\\System32\\Recovery\\Winre.wim");
        if (!found) {
            return std::unexpected(found.error());
        }
        if (!*found) {
            missing.push_back(image.index);
        }
    }
    if (!missing.empty()) {
        log::info("boot", std::format(L"{} edition(s) without WinRE: the new Setup cannot install them", missing.size()));
    }
    return missing;
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

namespace {

constexpr std::int32_t kKnownLcuFailure = static_cast<std::int32_t>(0x8007007E); // Microsoft: "a known issue … we can ignore"

std::wstring imageVersion(const std::filesystem::path& wim, int index) {
    auto file = DiskFile::open(nativePath(wim));
    if (!file) {
        return {};
    }
    auto read = readWim(**file);
    if (!read) {
        return {};
    }
    for (const auto& image : read->images) {
        if (image.index == index) {
            return image.versionString();
        }
    }
    return {};
}

// The cumulative update into a mounted WinPE: the combined update's first pass may stop at
// 0x8007007E once its servicing stack is in; the second pass then installs the rest.
Result<void> addCumulativeUpdate(Dism& dism, const std::filesystem::path& mountDir, const std::filesystem::path& lcu,
                                 const TaskContext& task) {
    auto session = dism.openSession(mountDir);
    if (!session) {
        return std::unexpected(session.error());
    }
    auto added = addPackageOrDismExe(**session, lcu, task);
    if (!added && added.error().hresult == kKnownLcuFailure) {
        log::info("boot", L"cumulative update: 0x8007007E on the first pass (known), adding it again");
        added = addPackageOrDismExe(**session, lcu, task);
    }
    if (!added) {
        return added;
    }
    auto cleaned = runDismExe(**session, L"/Cleanup-Image /StartComponentCleanup /ResetBase /Defer",
                              [&](double f) { task.report(f, L"cleanup"); });
    if (!cleaned) {
        return std::unexpected(cleaned.error());
    }
    if (cleaned->exitCode != 0) {
        return std::unexpected(dismExeFailure(*cleaned, L"boot image cleanup"));
    }
    return {};
}

// What the media must take from the updated Setup image (Microsoft, steps 27–28): Setup's files
// and the boot manager. Not only setup.exe / setuphost.exe as Microsoft's script copies them: 24H2+
// Setup copies the media to the disk and goes on from there, and its SetupHost refuses a Setup
// Platform of another build ("Determine if the expected version of Setup Platform has been
// loaded" → 0xC1900100, lab 2026-10-07) — so the media's sources\ takes all of Setup's sources\.
Result<void> saveSetupFiles(const std::filesystem::path& mountDir, const std::filesystem::path& to, int build) {
    std::error_code ec;
    std::filesystem::create_directories(to, ec);
    std::filesystem::remove_all(to / L"sources", ec);
    std::filesystem::copy(mountDir / L"sources", to / L"sources",
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot copy the updated Setup's sources", (mountDir / L"sources").wstring(), ec.value());
    }
    struct Item {
        const wchar_t* from;
        const wchar_t* name;
        bool required;
    };
    const Item items[] = {
        {L"sources\\setup.exe", L"sources\\setup.exe", true},
        {L"sources\\setuphost.exe", L"sources\\setuphost.exe", build >= 26100}, // 24H2+
        {L"Windows\\Boot\\EFI\\bootmgfw.efi", L"bootmgfw.efi", true},
        {L"Windows\\Boot\\EFI\\bootmgr.efi", L"bootmgr.efi", true},
        {L"Windows\\Boot\\EFI\\boot.stl", L"boot.stl", false},
    };
    for (const auto& item : items) {
        const auto source = mountDir / item.from;
        if (!std::filesystem::exists(source, ec)) {
            if (item.required) {
                return fail(ErrorCode::NotFound, L"the updated Setup image has no " + std::wstring(item.from), mountDir.wstring());
            }
            continue;
        }
        std::filesystem::copy_file(source, to / item.name, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail(ErrorCode::IoError, L"cannot copy " + std::wstring(item.from), source.wstring(), ec.value());
        }
    }
    return {};
}

int buildOf(const std::wstring& version) {
    // "10.0.26100.1742" → 26100
    const auto first = version.find(L'.');
    const auto second = first == std::wstring::npos ? first : version.find(L'.', first + 1);
    const auto third = second == std::wstring::npos ? second : version.find(L'.', second + 1);
    if (third == std::wstring::npos) {
        return 0;
    }
    return std::stoi(version.substr(second + 1, third - second - 1));
}

} // namespace

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
    log::info("boot", std::format(L"patch {} [{}]: {} LabConfig value(s), {} driver(s){}{}", bootWim.wstring(), *index,
                                  patch.labConfigValues().size(), patch.drivers.size(),
                                  patch.legacySetup ? L", previous Setup" : L"",
                                  patch.lcu.empty() ? L"" : L", cumulative update"));

    // Without an update only Setup's image changes; with one, every image of the file does.
    int count = *index;
    if (!patch.lcu.empty()) {
        auto file = DiskFile::open(bootWim);
        auto read = file ? readWim(**file) : Result<WimFile>(std::unexpected(file.error()));
        if (!read) {
            return std::unexpected(read.error());
        }
        count = static_cast<int>(read->images.size());
        report.versionBefore = imageVersion(bootWim, *index);
    }
    const int first = patch.lcu.empty() ? *index : 1;
    const double share = 1.0 / static_cast<double>(count - first + 1);
    for (int i = first; i <= count; ++i) {
        const double base = share * static_cast<double>(i - first);
        auto part = [&](double from, double to) {
            return TaskContext{task.cancel, [&task, base, share, from, to](double f, std::wstring_view s) {
                                   task.report(base + share * (from + (to - from) * f), s);
                               }};
        };
        const bool setupImage = i == *index;
        const bool updating = !patch.lcu.empty();
        if (auto mounted = mountSafely(dism, bootWim, i, mountDir, /*readOnly=*/false, part(0.0, updating ? 0.15 : 0.35)); !mounted) {
            return std::unexpected(mounted.error());
        }
        auto applied = [&]() -> Result<void> {
            if (updating) {
                if (auto lcu = addCumulativeUpdate(dism, mountDir, patch.lcu, part(0.15, 0.7)); !lcu) {
                    return lcu;
                }
                ++report.updated;
            }
            if (setupImage) {
                if (auto patched = applyPatch(dism, mountDir, patch, report, part(updating ? 0.7 : 0.35, updating ? 0.75 : 0.5));
                    !patched) {
                    return patched;
                }
                if (updating && !patch.setupFilesTo.empty()) {
                    return saveSetupFiles(mountDir, patch.setupFilesTo, buildOf(imageVersion(bootWim, i)));
                }
            }
            return {};
        }();
        // Not cancellable from here: a half-unmounted boot image helps nobody.
        const TaskContext unmountTask{CancelToken{}, [&](double f, std::wstring_view s) {
                                          task.report(base + share * ((updating ? 0.75 : 0.5) + (updating ? 0.25 : 0.5) * f), s);
                                      }};
        if (!applied) {
            log::error("boot", std::format(L"image {}: patch failed, discarding: {}", i, describe(applied.error())));
            if (auto discarded = unmountSafely(dism, mountDir, /*commit=*/false, unmountTask); !discarded) {
                log::error("boot", L"and the boot image could not be unmounted: " + describe(discarded.error()));
            }
            return std::unexpected(applied.error());
        }
        if (auto saved = unmountSafely(dism, mountDir, /*commit=*/true, unmountTask); !saved) {
            return std::unexpected(saved.error());
        }
    }
    if (!patch.lcu.empty()) {
        // A commit only appends: export every image into a new file (Microsoft, step 25), Setup's
        // image marked as the one the media boots.
        const auto rewritten = bootWim.parent_path() / (bootWim.stem().wstring() + L".new.wim");
        std::error_code ec;
        std::filesystem::remove(rewritten, ec);
        std::vector<int> all;
        for (int i = 1; i <= count; ++i) {
            all.push_back(i);
        }
        if (auto out = exportImages(bootWim, all, rewritten, WimCompression::Lzx, TaskContext{task.cancel, {}}); !out) {
            return std::unexpected(out.error());
        }
        if (auto marked = setBootImage(rewritten, *index); !marked) {
            return std::unexpected(marked.error());
        }
        std::filesystem::rename(rewritten, bootWim, ec);
        if (ec) {
            return fail(ErrorCode::IoError, L"cannot replace the boot image with its export", bootWim.wstring(), ec.value());
        }
        report.versionAfter = imageVersion(bootWim, *index);
        log::info("boot", std::format(L"Setup image {} → {}, {} image(s) updated", report.versionBefore, report.versionAfter,
                                      report.updated));
    }
    task.report(1.0, L"boot image");
    return report;
}

} // namespace wl::core
