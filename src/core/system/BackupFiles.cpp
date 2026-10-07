#include "core/system/BackupFiles.h"

#include <windows.h>

#include <string_view>

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
            const bool isDir = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool isLink = (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            // A linked folder is neither entered nor listed (it is not part of this tree).
            if (name != L"." && name != L".." && !(isDir && isLink)) {
                fn(name, isDir, static_cast<std::uint64_t>(entry->EndOfFile.QuadPart));
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

std::vector<std::wstring> backupListFiles(const std::filesystem::path& folder) {
    std::vector<std::wstring> names;
    enumerate(folder, [&](std::wstring_view name, bool isDir, std::uint64_t) {
        if (!isDir) {
            names.emplace_back(name);
        }
    });
    return names;
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

std::optional<std::string> backupReadFile(const std::filesystem::path& file, std::size_t limit) {
    const HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    std::optional<std::string> out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= limit) {
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (bytes.empty() || (ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size())) {
            out = std::move(bytes);
        }
    }
    CloseHandle(h);
    return out;
}

} // namespace wl::core
