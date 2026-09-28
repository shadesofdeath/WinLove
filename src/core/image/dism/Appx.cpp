#include "core/image/dism/Appx.h"

#include "base/Log.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace wl::core {

namespace {

// Calls fn(name, isDirectory, size) for each entry of `folder` (not "." / "..").
template <class F>
void enumerate(const std::filesystem::path& folder, F&& fn) {
    HANDLE dir = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir == INVALID_HANDLE_VALUE) {
        return;
    }
    alignas(8) BYTE buffer[64 * 1024];
    FILE_INFO_BY_HANDLE_CLASS cls = FileIdBothDirectoryRestartInfo;
    while (GetFileInformationByHandleEx(dir, cls, buffer, sizeof(buffer))) {
        cls = FileIdBothDirectoryInfo;
        auto* entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(buffer);
        for (;;) {
            const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                const bool isDir = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                const bool isLink = (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                fn(name, isDir && !isLink, static_cast<std::uint64_t>(entry->EndOfFile.QuadPart));
            }
            if (entry->NextEntryOffset == 0) {
                break;
            }
            entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(reinterpret_cast<BYTE*>(entry) + entry->NextEntryOffset);
        }
    }
    CloseHandle(dir);
}

} // namespace

std::uint64_t backupFolderSize(const std::filesystem::path& folder) {
    std::uint64_t total = 0;
    std::vector<std::filesystem::path> pending{folder};
    while (!pending.empty()) {
        const auto current = std::move(pending.back());
        pending.pop_back();
        enumerate(current, [&](std::wstring_view name, bool isDir, std::uint64_t size) {
            if (isDir) {
                pending.push_back(current / std::wstring(name));
            } else {
                total += size;
            }
        });
    }
    return total;
}

std::vector<std::wstring> backupListFolders(const std::filesystem::path& folder) {
    std::vector<std::wstring> names;
    enumerate(folder, [&](std::wstring_view name, bool isDir, std::uint64_t) {
        if (isDir) {
            names.emplace_back(name);
        }
    });
    return names;
}

Result<std::vector<AppxComponent>> readAppx(Dism& dism, const std::filesystem::path& mountDir, const TaskContext& task) {
    const auto started = std::chrono::steady_clock::now();
    auto session = dism.openSession(mountDir);
    if (!session) {
        return std::unexpected(session.error());
    }
    auto packages = (*session)->appxPackages();
    if (!packages) {
        return std::unexpected(packages.error());
    }
    (void)enablePrivilege(SE_BACKUP_NAME); // WindowsApps is ACL'd against administrators
    const auto windowsApps = mountDir / L"Program Files" / L"WindowsApps";
    const auto folders = backupListFolders(windowsApps);

    std::vector<AppxComponent> result;
    result.reserve(packages->size());
    for (std::size_t i = 0; i < packages->size(); ++i) {
        if (task.cancel.cancelled()) {
            return fail(ErrorCode::Cancelled, L"reading apps cancelled", mountDir.wstring());
        }
        AppxComponent item{(*packages)[i], 0};
        // Main + resource + per-architecture packages share the "<Name>_" prefix.
        const std::wstring prefix = item.package.displayName + L"_";
        for (const auto& folder : folders) {
            if (folder.size() > prefix.size() &&
                CompareStringOrdinal(folder.c_str(), static_cast<int>(prefix.size()), prefix.c_str(),
                                     static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL) {
                item.size += backupFolderSize(windowsApps / folder);
            }
        }
        result.push_back(std::move(item));
        task.report(static_cast<double>(i + 1) / static_cast<double>(packages->size()), L"appx");
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    log::info("dism", std::format(L"{} provisioned apps read in {} ms ({} WindowsApps folders)", result.size(), ms,
                                  folders.size()));
    return result;
}

} // namespace wl::core
