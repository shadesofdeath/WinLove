#include "core/image/dism/Appx.h"

#include "base/Log.h"
#include "core/image/OfflineHive.h"
#include "core/image/RegistryEdit.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
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

// ---- removal without DISM ----------------------------------------------------------------------

namespace {

constexpr const wchar_t* kAllUserStore = L"Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore";

// A package (full or family) name as a path component and a registry key name: nothing that
// could leave the folder or the key it is put under.
bool plainPackageName(std::wstring_view name) {
    return !name.empty() && name.size() <= 200 && name.front() != L'.' && name.back() != L'.' &&
           std::ranges::all_of(name, [](wchar_t c) {
               return c < 128 && (std::iswalnum(static_cast<wint_t>(c)) != 0 || c == L'.' || c == L'_' || c == L'-' || c == L'~');
           });
}

std::wstring lowerAscii(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return out;
}

} // namespace

std::wstring appxFamilyName(std::wstring_view packageFullName) {
    if (!plainPackageName(packageFullName)) {
        return {};
    }
    // Name _ Version _ Architecture _ ResourceId _ PublisherId; the name itself has no '_'.
    std::vector<std::wstring_view> parts;
    for (std::size_t start = 0; start <= packageFullName.size();) {
        const std::size_t end = std::min(packageFullName.find(L'_', start), packageFullName.size());
        parts.push_back(packageFullName.substr(start, end - start));
        start = end + 1;
    }
    if (parts.size() != 5 || parts.front().empty() || parts.back().empty()) {
        return {};
    }
    return std::wstring(parts.front()) + L"_" + std::wstring(parts.back());
}

ComponentRecipe appxRemovalRecipe(std::wstring_view packageFullName, const std::vector<std::wstring>& staged) {
    ComponentRecipe recipe;
    const std::wstring family = appxFamilyName(packageFullName);
    if (family.empty()) {
        return recipe;
    }
    recipe.title = std::wstring(packageFullName.substr(0, packageFullName.find(L'_')));
    auto folder = [&](std::wstring_view name) {
        // Only packages of this family: the list comes out of a registry that a preset's author
        // does not control, but a folder of another app must never ride along.
        if (appxFamilyName(name) != family) {
            return;
        }
        std::wstring path = L"Program Files\\WindowsApps\\" + std::wstring(name);
        if (std::ranges::find(recipe.paths, path) == recipe.paths.end()) {
            recipe.paths.push_back(std::move(path));
        }
    };
    folder(packageFullName);
    for (const auto& name : staged) {
        folder(name);
    }
    recipe.paths.push_back(L"ProgramData\\Microsoft\\Windows\\ClipSVC\\Install\\Apps\\" + lowerAscii(family) + L".xml");

    const std::wstring store = std::wstring(L"HKLM\\SOFTWARE\\") + kAllUserStore;
    auto key = [&](RegistryWrite::Kind kind, std::wstring path) {
        RegistryWrite write;
        write.kind = kind;
        write.key = std::move(path);
        recipe.registry.push_back(std::move(write));
    };
    key(RegistryWrite::Kind::DeleteKey, store + L"\\Applications\\" + std::wstring(packageFullName));
    key(RegistryWrite::Kind::DeleteKey, store + L"\\Staged\\" + family);
    key(RegistryWrite::Kind::CreateKey, store + L"\\Deprovisioned\\" + family);
    return recipe;
}

Result<std::vector<std::wstring>> readStagedAppx(const std::filesystem::path& mountDir, std::wstring_view family) {
    if (!plainPackageName(family)) {
        return fail(ErrorCode::InvalidArgument, L"not a package family name", std::wstring(family));
    }
    auto hive = OfflineHive::load(hiveFilePath(mountDir, OfflineHiveFile::Software));
    if (!hive) {
        return std::unexpected(hive.error());
    }
    auto staged = RegKey::open(hive->root(), std::wstring(kAllUserStore) + L"\\Staged\\" + std::wstring(family));
    if (!staged) {
        return std::vector<std::wstring>{}; // nothing staged under that name
    }
    return staged->subkeys(); // `staged` closes before `hive` unloads
}

Result<void> removeAppxNative(DismSession& session, std::wstring_view packageFullName, const TaskContext& task) {
    const std::wstring family = appxFamilyName(packageFullName);
    if (family.empty()) {
        return fail(ErrorCode::InvalidArgument, L"not a package full name", std::wstring(packageFullName));
    }
    session.suspend(); // DISM keeps the hives to itself
    auto staged = readStagedAppx(session.mountPath(), family);
    if (auto reopened = session.reload(); !reopened) {
        return reopened;
    }
    if (!staged) {
        return std::unexpected(staged.error());
    }
    const ComponentRecipe recipe = appxRemovalRecipe(packageFullName, *staged);
    log::info("appx", std::format(L"native removal of {}: {} folder(s) / file(s), {} registry change(s)", packageFullName,
                                  recipe.paths.size(), recipe.registry.size()));
    return removeComponent(session, recipe, task);
}

} // namespace wl::core
