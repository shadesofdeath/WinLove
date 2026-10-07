#include "core/image/dism/StoreShrink.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/system/FileLocks.h"
#include "core/system/Files.h"
#include "core/system/Handle.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <json.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace wl::core {

namespace {

using Json = nlohmann::json;

// The store's own folders.
constexpr std::array<std::wstring_view, 7> kStoreFolders{L"catalogs", L"filemaps", L"fusion", L"installtemp",
                                                         L"manifests", L"settingsmanifests", L"temp"};
constexpr std::array<std::wstring_view, 6> kArchitectures{L"amd64", L"x86", L"arm64", L"arm", L"wow64", L"msil"};
// Win32 side-by-side assemblies programs load from WinSxS itself (name_publicKeyToken_), as
// WinSxS spells them (long names are cut with ".."). tiny11's list, every architecture.
constexpr std::array<std::wstring_view, 7> kAssemblies{
    L"microsoft.windows.common-controls_6595b64144ccf1df_",
    L"microsoft.windows.c..-controls.resources_6595b64144ccf1df_",
    L"microsoft.windows.gdiplus_6595b64144ccf1df_",
    L"microsoft.windows.i..utomation.proxystub_6595b64144ccf1df_",
    L"microsoft.windows.isolationautomation_6595b64144ccf1df_",
    L"microsoft.vc80.crt_1fc8b3b9a1e18e3b_",
    L"microsoft.vc90.crt_1fc8b3b9a1e18e3b_",
};

bool keptAssembly(std::wstring_view rest) {
    return std::ranges::any_of(kAssemblies, [&](std::wstring_view a) { return rest.starts_with(a); });
}

// policy.<major>.<minor>.<assembly>: what sends a program asking for an older version to the one
// that is there.
bool keptPolicy(std::wstring_view rest) {
    if (!rest.starts_with(L"policy.")) {
        return false;
    }
    std::wstring_view tail = rest.substr(7);
    for (int part = 0; part < 2; ++part) { // the version's two numbers
        const auto dot = tail.find(L'.');
        if (dot == std::wstring_view::npos || dot == 0 ||
            !std::ranges::all_of(tail.substr(0, dot), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
            return false;
        }
        tail = tail.substr(dot + 1);
    }
    return keptAssembly(tail);
}

bool servicingStack(std::wstring_view rest) {
    return rest.starts_with(L"microsoft-windows-servicing") || rest.starts_with(L"microsoft-windows-s..ngstack") ||
           rest.starts_with(L"microsoft-windows-s..stack-");
}

struct Bytes {
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t unique = 0;
};

// Every file under `folder` with its link count, through backup semantics (WinSxS is closed to
// administrators). Linked folders are not entered.
void measureTree(const std::filesystem::path& folder, Bytes& out) {
    UniqueHandle dir{CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!dir) {
        return;
    }
    std::vector<std::filesystem::path> folders;
    alignas(8) BYTE buffer[64 * 1024];
    FILE_INFO_BY_HANDLE_CLASS cls = FileIdBothDirectoryRestartInfo;
    while (GetFileInformationByHandleEx(dir.get(), cls, buffer, sizeof(buffer))) {
        cls = FileIdBothDirectoryInfo;
        auto* entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(buffer);
        for (;;) {
            const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            const bool isDir = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool isLink = (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (name != L"." && name != L"..") {
                if (isDir) {
                    if (!isLink) {
                        folders.push_back(folder / std::wstring(name));
                    }
                } else {
                    ++out.files;
                    const auto size = static_cast<std::uint64_t>(entry->EndOfFile.QuadPart);
                    out.bytes += size;
                    UniqueHandle file{CreateFileW((folder / std::wstring(name)).c_str(), FILE_READ_ATTRIBUTES,
                                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
                    BY_HANDLE_FILE_INFORMATION info{};
                    if (file && GetFileInformationByHandle(file.get(), &info) && info.nNumberOfLinks <= 1) {
                        out.unique += size;
                    }
                }
            }
            if (entry->NextEntryOffset == 0) {
                break;
            }
            entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(reinterpret_cast<BYTE*>(entry) + entry->NextEntryOffset);
        }
    }
    dir = UniqueHandle{}; // closed before the subfolders are opened
    for (const auto& sub : folders) {
        measureTree(sub, out);
    }
}

// The store's folder of a mounted Windows image; refuses anything else.
Result<std::filesystem::path> storeFolder(const std::filesystem::path& mountDir) {
    if (!std::filesystem::exists(mountDir / L"Windows" / L"System32" / L"config" / L"SOFTWARE")) {
        return fail(ErrorCode::InvalidArgument, L"not a mounted Windows image", mountDir.wstring());
    }
    const auto store = mountDir / L"Windows" / L"WinSxS";
    if (!std::filesystem::is_directory(store) || isReparsePoint(mountDir / L"Windows") || isReparsePoint(store)) {
        return fail(ErrorCode::InvalidArgument, L"the image has no component store of its own", store.wstring());
    }
    if (!std::filesystem::exists(store / L"Manifests")) {
        return fail(ErrorCode::InvalidArgument, L"not a component store (no Manifests)", store.wstring());
    }
    return store;
}

std::vector<std::wstring> topFolders(const std::filesystem::path& store) {
    std::vector<std::wstring> names;
    UniqueHandle dir{CreateFileW(store.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!dir) {
        return names;
    }
    alignas(8) BYTE buffer[64 * 1024];
    FILE_INFO_BY_HANDLE_CLASS cls = FileIdBothDirectoryRestartInfo;
    while (GetFileInformationByHandleEx(dir.get(), cls, buffer, sizeof(buffer))) {
        cls = FileIdBothDirectoryInfo;
        auto* entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(buffer);
        for (;;) {
            const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if ((entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && name != L"." && name != L"..") {
                names.emplace_back(name);
            }
            if (entry->NextEntryOffset == 0) {
                break;
            }
            entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(reinterpret_cast<BYTE*>(entry) + entry->NextEntryOffset);
        }
    }
    std::ranges::sort(names);
    return names;
}

// Deletes one name with SeRestore's DELETE right (no ownership or ACL change); POSIX semantics,
// so a file still open somewhere does not block its folder.
bool unlinkWithRestore(const std::filesystem::path& path) {
    UniqueHandle handle{CreateFileW(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!handle) {
        return false;
    }
    FILE_DISPOSITION_INFO_EX ex{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                                FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
    return SetFileInformationByHandle(handle.get(), FileDispositionInfoEx, &ex, sizeof(ex)) != 0;
}

// A folder and everything in it; never through a link (a linked folder's name is removed, not
// its target). Falls back to the take-ownership removal for what restore semantics cannot.
Result<void> removeTree(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> folders;
    {
        UniqueHandle dir{CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (!dir) {
            return forceRemoveEntry(folder);
        }
        alignas(8) BYTE buffer[64 * 1024];
        FILE_INFO_BY_HANDLE_CLASS cls = FileIdBothDirectoryRestartInfo;
        while (GetFileInformationByHandleEx(dir.get(), cls, buffer, sizeof(buffer))) {
            cls = FileIdBothDirectoryInfo;
            auto* entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(buffer);
            for (;;) {
                const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
                if (name != L"." && name != L"..") {
                    const bool isDir = (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    const bool isLink = (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                    (isDir && !isLink ? folders : files).push_back(folder / std::wstring(name));
                }
                if (entry->NextEntryOffset == 0) {
                    break;
                }
                entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(reinterpret_cast<BYTE*>(entry) + entry->NextEntryOffset);
            }
        }
    }
    for (const auto& sub : folders) {
        if (auto gone = removeTree(sub); !gone) {
            return gone;
        }
    }
    for (const auto& file : files) {
        if (!unlinkWithRestore(file)) {
            if (auto gone = forceRemoveEntry(file); !gone) {
                return gone;
            }
        }
    }
    if (!unlinkWithRestore(folder)) {
        return forceRemoveEntry(folder);
    }
    return {};
}

} // namespace

bool keptInShrunkStore(std::wstring_view entry) {
    const std::wstring name = text::lower(std::wstring(entry));
    if (std::ranges::find(kStoreFolders, std::wstring_view(name)) != kStoreFolders.end()) {
        return true;
    }
    const auto underscore = name.find(L'_');
    if (underscore == std::wstring::npos ||
        std::ranges::find(kArchitectures, std::wstring_view(name).substr(0, underscore)) == kArchitectures.end()) {
        return false;
    }
    const std::wstring_view rest = std::wstring_view(name).substr(underscore + 1);
    return keptAssembly(rest) || keptPolicy(rest) || servicingStack(rest);
}

Result<StoreShrinkPlan> planStoreShrink(const std::filesystem::path& mountDir, bool measure, const TaskContext& task) {
    auto store = storeFolder(mountDir);
    if (!store) {
        return std::unexpected(store.error());
    }
    (void)enablePrivilege(SE_BACKUP_NAME);
    StoreShrinkPlan plan;
    for (auto& name : topFolders(*store)) {
        if (keptInShrunkStore(name)) {
            ++plan.kept;
        } else {
            plan.removed.push_back(std::move(name));
        }
    }
    log::info("store", std::format(L"WinSxS of {}: {} folders stay, {} go", mountDir.wstring(), plan.kept, plan.removed.size()));
    if (measure) {
        Bytes bytes;
        for (std::size_t i = 0; i < plan.removed.size(); ++i) {
            if (auto go = task.cancel.check(L"measuring the component store"); !go) {
                return std::unexpected(go.error());
            }
            measureTree(*store / plan.removed[i], bytes);
            if (i % 256 == 0) {
                task.report(static_cast<double>(i) / static_cast<double>(plan.removed.size()), L"WinSxS");
            }
        }
        plan.files = bytes.files;
        plan.bytes = bytes.bytes;
        plan.freed = bytes.unique;
    }
    return plan;
}

Result<StoreShrinkPlan> shrinkComponentStore(const std::filesystem::path& mountDir, const TaskContext& task) {
    // Measured first: once a folder is gone, what its files shared is no longer visible.
    auto plan = planStoreShrink(mountDir, /*measure=*/true,
                                TaskContext{task.cancel, [&](double f, std::wstring_view s) { task.report(0.3 * f, s); }});
    if (!plan) {
        return plan;
    }
    // A store that would keep nothing it needs (an unknown layout) is not touched.
    const auto store = mountDir / L"Windows" / L"WinSxS";
    // Pending servicing (a feature enabled in this run, .NET 3.5) finishes at the first boot from
    // the payload in WinSxS: without it setup would fail.
    if (std::filesystem::exists(store / L"pending.xml")) {
        return fail(ErrorCode::Unsupported,
                    L"the image has pending servicing operations (WinSxS\\pending.xml): they need the store at the first boot",
                    store.wstring());
    }
    const bool hasStack = std::ranges::any_of(topFolders(store), [](const std::wstring& n) {
        return keptInShrunkStore(n) && text::lower(n).find(L"servicingstack") != std::wstring::npos;
    });
    if (!hasStack) {
        return fail(ErrorCode::Unsupported, L"no servicing stack found in WinSxS: not shrinking an unknown store layout",
                    store.wstring());
    }
    (void)enablePrivilege(SE_BACKUP_NAME);
    (void)enablePrivilege(SE_RESTORE_NAME);
    log::info("store", std::format(L"shrinking WinSxS: {} folders go, {} stay; {:.2f} GB freed of {:.2f} GB", plan->removed.size(),
                                   plan->kept, static_cast<double>(plan->freed) / (1024.0 * 1024 * 1024),
                                   static_cast<double>(plan->bytes) / (1024.0 * 1024 * 1024)));
    for (std::size_t i = 0; i < plan->removed.size(); ++i) {
        if (auto go = task.cancel.check(L"shrinking the component store"); !go) {
            return std::unexpected(go.error());
        }
        if (auto gone = removeTree(store / plan->removed[i]); !gone) {
            return std::unexpected(gone.error());
        }
        if (i % 64 == 0) {
            task.report(0.3 + 0.7 * static_cast<double>(i) / static_cast<double>(plan->removed.size()), L"WinSxS");
        }
    }
    task.report(1.0, L"WinSxS");
    return plan;
}

std::string storeShrinkToJson(const StoreShrinkOptions& options) {
    return Json{{"title", utf8::fromWide(options.title)}}.dump();
}

Result<StoreShrinkOptions> storeShrinkFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"not a store shrink operation");
    }
    StoreShrinkOptions options;
    try {
        options.title = utf8::toWide(doc.value("title", std::string{}));
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed store shrink operation", utf8::toWide(e.what()));
    }
    return options;
}

} // namespace wl::core
