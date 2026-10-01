#include "core/image/dism/MountHealth.h"

#include "base/Log.h"
#include "base/Path.h"
#include "base/Text.h"
#include "core/image/Source.h"
#include "core/image/dism/DismErrors.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <format>

namespace wl::core {

namespace {

bool samePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    auto norm = [](const std::filesystem::path& p) {
        std::wstring s = p.lexically_normal().wstring();
        while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) {
            s.pop_back();
        }
        return s;
    };
    return _wcsicmp(norm(a).c_str(), norm(b).c_str()) == 0;
}

bool hasEntries(const std::filesystem::path& folder) {
    std::error_code ec;
    return std::filesystem::is_directory(folder, ec) && !std::filesystem::is_empty(folder, ec) && !ec;
}

bool fileExists(const std::filesystem::path& file) {
    std::error_code ec;
    return std::filesystem::is_regular_file(file, ec);
}

// C:\WinLove\mount → \Device\HarddiskVolume3\WinLove\mount (the form hivelist uses).
std::wstring devicePath(const std::filesystem::path& folder) {
    const std::wstring full = std::filesystem::absolute(folder).lexically_normal().wstring();
    if (full.size() < 2 || full[1] != L':') {
        return full;
    }
    wchar_t device[MAX_PATH]{};
    const std::wstring drive = full.substr(0, 2);
    if (QueryDosDeviceW(drive.c_str(), device, MAX_PATH) == 0) {
        return full;
    }
    return std::wstring(device) + full.substr(2);
}

MountCheck check(const std::filesystem::path& folder, std::optional<MountInfo> record) {
    MountCheck c;
    c.folder = folder;
    c.state = classifyMount(record, record && fileExists(record->imagePath), hasEntries(folder));
    c.action = recommendedAction(c.state);
    c.windowsImage = fileExists(folder / L"Windows" / L"System32" / L"config" / L"SOFTWARE");
    c.loadedHives = hivesLoadedFrom(folder);
    // Only where it helps a decision: the scan walks the folder and asks Restart Manager.
    if (c.state != MountState::Free && c.state != MountState::Ok) {
        c.blockers = blockersOf(folder);
    }
    if (record && fileExists(record->imagePath)) {
        if (auto info = openSource(record->imagePath)) {
            for (const auto& image : info->install.images) {
                if (image.index == record->index) {
                    c.imageName = image.name;
                }
            }
        }
    }
    c.record = std::move(record);
    return c;
}

} // namespace

const wchar_t* mountStateName(MountState state) noexcept {
    switch (state) {
    case MountState::Free: return L"free";
    case MountState::Ok: return L"ok";
    case MountState::NeedsRemount: return L"needs remount";
    case MountState::Invalid: return L"invalid";
    case MountState::ImageMissing: return L"image missing";
    case MountState::Orphaned: return L"orphaned";
    }
    return L"?";
}

const wchar_t* mountActionName(MountAction action) noexcept {
    switch (action) {
    case MountAction::None: return L"none";
    case MountAction::Remount: return L"remount";
    case MountAction::Discard: return L"discard";
    case MountAction::ClearFolder: return L"clear folder";
    }
    return L"?";
}

MountAction recommendedAction(MountState state) noexcept {
    switch (state) {
    case MountState::Free:
    case MountState::Ok: return MountAction::None;
    case MountState::NeedsRemount: return MountAction::Remount;
    case MountState::Invalid:
    case MountState::ImageMissing: return MountAction::Discard;
    case MountState::Orphaned: return MountAction::ClearFolder;
    }
    return MountAction::None;
}

MountState classifyMount(const std::optional<MountInfo>& record, bool imageExists, bool folderHasEntries) noexcept {
    if (!record) {
        return folderHasEntries ? MountState::Orphaned : MountState::Free;
    }
    switch (record->status) {
    case DismMountStatus::Invalid: return MountState::Invalid;
    case DismMountStatus::NeedsRemount:
        // Remounting needs the WIM; without it only discarding is left.
        return imageExists ? MountState::NeedsRemount : MountState::ImageMissing;
    case DismMountStatus::Ok: return imageExists ? MountState::Ok : MountState::ImageMissing;
    }
    return MountState::Invalid;
}

Result<MountCheck> inspectMount(Dism& dism, const std::filesystem::path& folderInput) {
    const std::filesystem::path folder = nativePath(folderInput);
    auto mounts = dism.mounts();
    if (!mounts) {
        return std::unexpected(mounts.error());
    }
    for (auto& m : *mounts) {
        if (samePath(m.mountPath, folder)) {
            return check(folder, std::move(m));
        }
    }
    return check(folder, std::nullopt);
}

Result<std::vector<MountCheck>> inspectMounts(Dism& dism) {
    auto mounts = dism.mounts();
    if (!mounts) {
        return std::unexpected(mounts.error());
    }
    std::vector<MountCheck> result;
    for (auto& m : *mounts) {
        const auto folder = m.mountPath;
        result.push_back(check(folder, std::move(m)));
    }
    return result;
}

std::vector<std::wstring> hivesLoadedFrom(const std::filesystem::path& folder) {
    std::vector<std::wstring> result;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\hivelist", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return result;
    }
    const std::wstring prefix = devicePath(folder) + L"\\";
    for (DWORD i = 0;; ++i) {
        wchar_t name[512]{};
        wchar_t data[1024]{};
        DWORD nameLength = 512;
        DWORD dataBytes = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        const LSTATUS status =
            RegEnumValueW(key, i, name, &nameLength, nullptr, &type, reinterpret_cast<BYTE*>(data), &dataBytes);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status == ERROR_SUCCESS && type == REG_SZ && text::istartsWith(data, prefix)) {
            result.emplace_back(name);
        }
    }
    RegCloseKey(key);
    return result;
}

Result<void> unloadHivesUnder(const std::filesystem::path& folder) {
    const auto hives = hivesLoadedFrom(folder);
    if (hives.empty()) {
        return {};
    }
    (void)enablePrivilege(SE_BACKUP_NAME);
    (void)enablePrivilege(SE_RESTORE_NAME);
    constexpr std::wstring_view kMachine = L"\\REGISTRY\\MACHINE\\";
    constexpr std::wstring_view kUser = L"\\REGISTRY\\USER\\";
    for (const auto& hive : hives) {
        HKEY root = nullptr;
        std::wstring sub;
        if (text::istartsWith(hive, kMachine)) {
            root = HKEY_LOCAL_MACHINE;
            sub = hive.substr(kMachine.size());
        } else if (text::istartsWith(hive, kUser)) {
            root = HKEY_USERS;
            sub = hive.substr(kUser.size());
        } else {
            continue;
        }
        log::info("dism", L"unloading hive " + hive);
        if (const LSTATUS status = RegUnLoadKeyW(root, sub.c_str()); status != ERROR_SUCCESS) {
            return fail(ErrorCode::AccessDenied, L"could not unload registry hive (a handle is still open)", hive,
                        static_cast<std::int32_t>(HRESULT_FROM_WIN32(status)));
        }
    }
    return {};
}

Result<MountCheck> repairMount(Dism& dism, const MountCheck& check, const TaskContext& task) {
    log::info("dism", std::format(L"repair {}: {} → {}", check.folder.wstring(), mountStateName(check.state),
                                  mountActionName(check.action)));
    if (check.action != MountAction::None) {
        releaseExplorerWindows(check.folder);
    }
    switch (check.action) {
    case MountAction::None: break;
    case MountAction::Remount:
        if (auto r = dism.remount(check.folder); !r) {
            return std::unexpected(r.error());
        }
        break;
    case MountAction::Discard:
        if (auto r = unloadHivesUnder(check.folder); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = dism.unmount(check.folder, /*commit=*/false, task); !r) {
            // An invalid mount often refuses a normal unmount; cleanup below still frees it.
            log::warn("dism", describe(r.error()));
        }
        if (auto r = dism.cleanupMountpoints(); !r) {
            return std::unexpected(r.error());
        }
        break;
    case MountAction::ClearFolder:
        if (auto r = dism.cleanupMountpoints(); !r) {
            return std::unexpected(r.error());
        }
        break;
    }
    auto after = inspectMount(dism, check.folder);
    // Discard/cleanup detach the image but can leave files (the partial-unmount case): with no
    // DISM record left, what remains is plain leftovers and is safe to delete.
    if (after && after->state == MountState::Orphaned && check.action != MountAction::None &&
        check.action != MountAction::Remount) {
        if (auto r = forceRemoveContents(check.folder); !r) {
            return std::unexpected(r.error());
        }
        after = inspectMount(dism, check.folder);
    }
    return after;
}

Result<UnmountOutcome> unmountSafely(Dism& dism, const std::filesystem::path& folderInput, bool commit,
                                     const TaskContext& task) {
    const std::filesystem::path folder = nativePath(folderInput);
    UnmountOutcome outcome;
    if (auto r = unloadHivesUnder(folder); !r) {
        return std::unexpected(r.error());
    }
    releaseExplorerWindows(folder);
    Error last;
    bool sawPartial = false;   // an earlier attempt committed but left files behind
    bool notCommitted = false; // detached without a commit ever succeeding
    for (int attempt = 0; attempt < 3; ++attempt) {
        ++outcome.attempts;
        auto r = dism.unmount(folder, commit, task);
        if (r) {
            break;
        }
        last = r.error();
        if (last.code == ErrorCode::Cancelled || task.cancel.cancelled()) {
            return std::unexpected(last);
        }
        const auto info = explainError(last.hresult);
        log::warn("dism", std::format(L"unmount attempt {} failed: 0x{:08X} {}", outcome.attempts,
                                      static_cast<std::uint32_t>(last.hresult), info.label));
        outcome.recovered = true;
        if (last.hresult == kPartialUnmount || last.hresult == kFileInUse) {
            sawPartial = sawPartial || last.hresult == kPartialUnmount;
            // Something still holds files: move Explorer away, give handles time to close, retry.
            releaseExplorerWindows(folder);
            Sleep(1500);
            continue;
        }
        if (last.hresult == kCannotCommit || last.hresult == kNotMounted || last.hresult == kNotMountDir) {
            // The image is already detached (earlier partial unmount): only leftovers remain.
            // Without such an earlier attempt nothing was saved: never report a commit then.
            notCommitted = commit && !sawPartial;
            break;
        }
        return std::unexpected(last); // any other error: report as is
    }
    // Whatever happened, the folder must end up Free. If the image is still attached, the retries
    // did not help: report the last error together with who is blocking.
    auto check = inspectMount(dism, folder);
    if (!check) {
        return std::unexpected(check.error());
    }
    auto uncommitted = [&]() -> Result<UnmountOutcome> {
        return fail(ErrorCode::DismFailure, L"the image was detached but the changes could not be committed",
                    folder.wstring(), last.hresult);
    };
    if (check->state == MountState::Free) {
        if (notCommitted) {
            return uncommitted();
        }
        return outcome;
    }
    if (check->state == MountState::Ok || check->state == MountState::NeedsRemount) {
        std::wstring who;
        for (const auto& b : blockersOf(folder)) { // not in check(): healthy mounts skip the scan
            who += (who.empty() ? L"" : L", ") + b.name;
        }
        if (!who.empty()) {
            last.context += L" — in use by: " + who;
        }
        return std::unexpected(last);
    }
    outcome.recovered = true;
    auto repaired = repairMount(dism, *check, task);
    if (!repaired) {
        return std::unexpected(repaired.error());
    }
    if (repaired->state != MountState::Free) {
        return fail(ErrorCode::DismFailure, L"mount folder could not be freed",
                    std::format(L"{} ({})", folder.wstring(), mountStateName(repaired->state)), last.hresult);
    }
    if (notCommitted || (commit && !sawPartial && check->action == MountAction::Discard)) {
        return uncommitted(); // folder is clean again, but the repair discarded the image
    }
    return outcome;
}

namespace {

// Deletes the (empty or leftover-only) mount folder itself and creates it again.
Result<void> recreateFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    if (std::filesystem::exists(folder, ec)) {
        if (auto r = forceRemoveContents(folder); !r) {
            return r;
        }
        if (!RemoveDirectoryW(folder.c_str())) {
            const DWORD status = GetLastError();
            log::warn("dism", std::format(L"could not recreate {} (0x{:08X}); reusing it", folder.wstring(),
                                          static_cast<std::uint32_t>(HRESULT_FROM_WIN32(status))));
            return {};
        }
    }
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"could not create the mount folder", folder.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value())));
    }
    return {};
}

bool folderBusyCode(std::int32_t hr) {
    switch (static_cast<std::uint32_t>(hr)) {
    case 0xC1420113: // directory already contains a mounted image
    case 0xC1420114: // directory not empty
    case 0xC1420103: // directory already exists
    case 0xC1420120: // corrupted mount in directory
    case 0x800704D3: // stub refused (folder in use)
    case 0x80070005: // access denied in the folder
        return true;
    default: return false;
    }
}

} // namespace

Result<MountOutcome> mountSafely(Dism& dism, const std::filesystem::path& wimInput, int index,
                                 const std::filesystem::path& folderInput, bool readOnly, const TaskContext& task) {
    const std::filesystem::path wim = nativePath(wimInput);
    const std::filesystem::path folder = nativePath(folderInput);
    MountOutcome outcome;
    releaseExplorerWindows(folder);

    auto check = inspectMount(dism, folder);
    if (!check) {
        return std::unexpected(check.error());
    }
    if ((check->state == MountState::Ok || check->state == MountState::NeedsRemount) && check->record &&
        _wcsicmp(nativePath(check->record->imagePath).c_str(), wim.c_str()) == 0 && check->record->index == index) {
        // Same image already there (e.g. the app was closed while mounted): use it.
        if (check->state == MountState::NeedsRemount) {
            if (auto r = dism.remount(folder); !r) {
                return std::unexpected(r.error());
            }
        }
        outcome.reused = true;
        return outcome;
    }
    if (check->state == MountState::Ok || check->state == MountState::NeedsRemount) {
        return fail(ErrorCode::DismFailure, L"another image is mounted in the mount folder",
                    std::format(L"{} holds {} index {}", folder.wstring(),
                                check->record ? check->record->imagePath.wstring() : L"?",
                                check->record ? check->record->index : 0),
                    static_cast<std::int32_t>(0xC1420113));
    }
    if (check->state != MountState::Free) {
        outcome.recovered = true;
        auto repaired = repairMount(dism, *check, task);
        if (!repaired) {
            return std::unexpected(repaired.error());
        }
        if (repaired->state != MountState::Free) {
            // Never clear files out of a folder DISM still has attached.
            return fail(ErrorCode::DismFailure, L"mount folder could not be freed",
                        std::format(L"{} ({})", folder.wstring(), mountStateName(repaired->state)));
        }
    }
    if (auto r = recreateFolder(folder); !r) {
        return std::unexpected(r.error());
    }
    auto mounted = dism.mount(wim, index, folder, readOnly, task);
    if (!mounted && folderBusyCode(mounted.error().hresult) && !task.cancel.cancelled()) {
        log::warn("dism", std::format(L"mount refused (0x{:08X}); cleaning up and retrying once",
                                      static_cast<std::uint32_t>(mounted.error().hresult)));
        outcome.recovered = true;
        (void)dism.cleanupMountpoints();
        releaseExplorerWindows(folder);
        if (auto r = recreateFolder(folder); !r) {
            return std::unexpected(r.error());
        }
        mounted = dism.mount(wim, index, folder, readOnly, task);
    }
    if (!mounted) {
        return std::unexpected(mounted.error());
    }
    return outcome;
}

} // namespace wl::core
