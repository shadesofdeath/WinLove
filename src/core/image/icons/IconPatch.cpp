#include "core/image/icons/IconPatch.h"

#include "base/File.h"
#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/icons/IconSource.h"
#include "core/image/SystemComponents.h"
#include "core/system/Handle.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <aclapi.h>

#include <json.hpp>

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <memory>

namespace wl::core {

namespace {

#ifndef FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE
#define FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE 0x00000004
#endif

constexpr std::wstring_view kBackupRoot = L"Windows\\WinLove\\IconBackup";
constexpr std::wstring_view kRestoreScript = L"restore-icons.cmd";

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    std::ranges::transform(out, out.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return out;
}

std::wstring normalized(std::wstring_view relative) {
    std::wstring out(relative);
    std::ranges::replace(out, L'/', L'\\');
    while (!out.empty() && out.front() == L'\\') {
        out.erase(out.begin());
    }
    return out;
}

void enableBackupRestore() {
    static const bool once = [] {
        (void)enablePrivilege(SE_BACKUP_NAME);
        (void)enablePrivilege(SE_RESTORE_NAME);
        (void)enablePrivilege(SE_TAKE_OWNERSHIP_NAME);
        return true;
    }();
    (void)once;
}

Error win32(const wchar_t* what, const std::filesystem::path& path, DWORD error = GetLastError()) {
    return Error{ErrorCode::IoError, what, path.wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(error))};
}

// Reads a file whatever its ACL says (backup semantics).
Result<std::string> readProtected(const std::filesystem::path& path) {
    enableBackupRestore();
    UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (!file) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return fail(ErrorCode::NotFound, L"file not found", path.wstring());
        }
        return std::unexpected(win32(L"cannot read the file", path, error));
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart > (1ll << 30)) {
        return fail(ErrorCode::IoError, L"cannot size the file", path.wstring());
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t done = 0;
    while (done < bytes.size()) {
        DWORD got = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 24));
        if (!ReadFile(file.get(), bytes.data() + done, chunk, &got, nullptr) || got == 0) {
            return std::unexpected(win32(L"cannot read the file", path));
        }
        done += got;
    }
    return bytes;
}

struct SecurityCopy {
    std::unique_ptr<std::byte[]> buffer;
    PSID owner = nullptr;
    PSID group = nullptr;
    PACL dacl = nullptr;
    bool protectedDacl = false;
};

Result<SecurityCopy> securityOf(const std::filesystem::path& path) {
    enableBackupRestore();
    const SECURITY_INFORMATION what = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    DWORD needed = 0;
    GetFileSecurityW(path.c_str(), what, nullptr, 0, &needed);
    if (needed == 0) {
        return std::unexpected(win32(L"cannot read the file's security", path));
    }
    SecurityCopy copy;
    copy.buffer = std::make_unique<std::byte[]>(needed);
    auto* sd = reinterpret_cast<PSECURITY_DESCRIPTOR>(copy.buffer.get());
    if (!GetFileSecurityW(path.c_str(), what, sd, needed, &needed)) {
        return std::unexpected(win32(L"cannot read the file's security", path));
    }
    BOOL defaulted = FALSE;
    BOOL present = FALSE;
    GetSecurityDescriptorOwner(sd, &copy.owner, &defaulted);
    GetSecurityDescriptorGroup(sd, &copy.group, &defaulted);
    GetSecurityDescriptorDacl(sd, &present, &copy.dacl, &defaulted);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    GetSecurityDescriptorControl(sd, &control, &revision);
    copy.protectedDacl = (control & SE_DACL_PROTECTED) != 0;
    return copy;
}

bool applySecurity(HANDLE file, const SecurityCopy& security) {
    return SetKernelObjectSecurity(file, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                   reinterpret_cast<PSECURITY_DESCRIPTOR>(security.buffer.get())) != 0;
}

// `bytes` take the place of `target` (see the header, step 4). `security`: the owner / group / DACL
// the new file gets. On a failed rename the original name is put back from `fallback`.
Result<void> writeReplacing(const std::filesystem::path& target, std::string_view bytes, const SecurityCopy& security,
                            const std::function<Result<IconLoadCheck>(const std::filesystem::path&)>& verify,
                            std::string_view fallback) {
    enableBackupRestore();
    std::filesystem::path temp = target;
    temp += L".wlnew";
    DeleteFileW(temp.c_str());
    {
        UniqueHandle file{CreateFileW(temp.c_str(), GENERIC_WRITE | WRITE_DAC | WRITE_OWNER, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
        if (!file) {
            return std::unexpected(win32(L"cannot create the new file beside the original", temp));
        }
        std::size_t done = 0;
        while (done < bytes.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 24));
            DWORD written = 0;
            if (!WriteFile(file.get(), bytes.data() + done, chunk, &written, nullptr) || written != chunk) {
                const Error error = win32(L"cannot write the new file", temp);
                file = UniqueHandle{};
                DeleteFileW(temp.c_str());
                return std::unexpected(error);
            }
            done += written;
        }
        // The descriptor exactly as the original has it (SetSecurityInfo would recompute the
        // inheritance flags); setting TrustedInstaller as owner needs SeRestorePrivilege.
        if (!applySecurity(file.get(), security)) {
            const DWORD set = GetLastError();
            file = UniqueHandle{};
            DeleteFileW(temp.c_str());
            return std::unexpected(win32(L"cannot give the new file the original's owner and permissions", temp, set));
        }
    }
    if (verify) {
        auto check = verify(temp);
        if (!check || check->failed > 0 || check->groups == 0) {
            DeleteFileW(temp.c_str());
            if (!check) {
                return std::unexpected(check.error());
            }
            return fail(ErrorCode::InvalidArgument, L"Windows could not load the patched file's icons; nothing was changed",
                        check->firstFailure);
        }
    }
    // Rename over the target: only this name changes, a hard link's other name (WinSxS) keeps the original.
    auto renameOver = [&](bool replace) -> bool {
        UniqueHandle file{CreateFileW(temp.c_str(), DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
        if (!file) {
            return false;
        }
        const std::wstring name = target.wstring();
        std::vector<std::byte> buffer(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
        auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        info->Flags = (replace ? FILE_RENAME_FLAG_REPLACE_IF_EXISTS : 0) | FILE_RENAME_FLAG_POSIX_SEMANTICS |
                      FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE;
        info->RootDirectory = nullptr;
        info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
        std::memcpy(info->FileName, name.data(), name.size() * sizeof(wchar_t));
        return SetFileInformationByHandle(file.get(), FileRenameInfoEx, info, static_cast<DWORD>(buffer.size())) != 0;
    };
    if (renameOver(true)) {
        return {};
    }
    // Access denied on the old name: remove just that name (backup semantics), then rename.
    {
        UniqueHandle old{CreateFileW(target.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        FILE_DISPOSITION_INFO_EX ex{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                                    FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
        if (!old || !SetFileInformationByHandle(old.get(), FileDispositionInfoEx, &ex, sizeof(ex))) {
            const Error error = win32(L"cannot replace the original file", target);
            DeleteFileW(temp.c_str());
            return std::unexpected(error);
        }
    }
    if (renameOver(false)) {
        return {};
    }
    const Error error = win32(L"cannot move the new file into place", target);
    DeleteFileW(temp.c_str());
    // The old name is gone: the original bytes go back under it (with its security) before failing.
    UniqueHandle file{CreateFileW(target.c_str(), GENERIC_WRITE | WRITE_DAC | WRITE_OWNER, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (file) {
        DWORD written = 0;
        WriteFile(file.get(), fallback.data(), static_cast<DWORD>(fallback.size()), &written, nullptr);
        (void)applySecurity(file.get(), security);
    } else {
        log::error("icons", L"the original could not be put back: " + target.wstring());
    }
    return std::unexpected(error);
}

// Hard links of the file: 2+ means servicing owns it again (an update put its own version in,
// linked into WinSxS) — what we patched is gone.
int linkCount(const std::filesystem::path& path) {
    enableBackupRestore();
    UniqueHandle file{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!file || !GetFileInformationByHandle(file.get(), &info)) {
        return 0;
    }
    return static_cast<int>(info.nNumberOfLinks);
}

bool sameLeaves(const ResourceType& a, const ResourceType& b) {
    if (a.key != b.key || a.names.size() != b.names.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.names.size(); ++i) {
        const auto& x = a.names[i];
        const auto& y = b.names[i];
        if (x.key != y.key || x.languages.size() != y.languages.size()) {
            return false;
        }
        for (std::size_t j = 0; j < x.languages.size(); ++j) {
            if (x.languages[j].language != y.languages[j].language || x.languages[j].codePage != y.languages[j].codePage ||
                x.languages[j].data != y.languages[j].data) {
                return false;
            }
        }
    }
    return true;
}

Result<void> writeRestoreScript(const std::filesystem::path& mountDir) {
    const auto files = patchedIconFiles(mountDir);
    auto root = resolveImagePath(mountDir, kBackupRoot);
    if (!root) {
        return std::unexpected(root.error());
    }
    const auto script = *root / kRestoreScript;
    if (files.empty()) {
        std::error_code ec;
        std::filesystem::remove(script, ec);
        return {};
    }
    std::string text =
        "@echo off\r\n"
        "rem WinLove (D-068): puts the original icon files back. Run it as administrator on the installed\r\n"
        "rem system, or from WinRE / setup media as  <drive>:\\Windows\\WinLove\\IconBackup\\restore-icons.cmd\r\n"
        "setlocal\r\n"
        "set \"BACKUP=%~dp0\"\r\n"
        "for %%I in (\"%~dp0..\\..\") do set \"WIN=%%~fI\"\r\n";
    for (const auto& file : files) {
        // "Windows\SystemResources\imageres.dll.mun" → "SystemResources\imageres.dll.mun"
        const std::wstring underWindows = file.substr(std::wstring_view(L"Windows\\").size());
        std::string narrow;
        for (const wchar_t c : underWindows) {
            narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        text += "call :restore \"" + narrow + "\"\r\n";
    }
    text +=
        "echo Done. Restart Windows (or Explorer) to see the original icons.\r\n"
        "exit /b 0\r\n"
        ":restore\r\n"
        "set \"T=%WIN%\\%~1\"\r\n"
        "if not exist \"%BACKUP%%~1\" (echo missing backup: %~1& exit /b 1)\r\n"
        "icacls \"%T%\" /save \"%TEMP%\\wl-icon-acl.txt\" >nul 2>&1\r\n"
        "takeown /f \"%T%\" /a >nul 2>&1\r\n"
        "icacls \"%T%\" /grant *S-1-5-32-544:F >nul 2>&1\r\n"
        "copy /b /y \"%BACKUP%%~1\" \"%T%\" >nul || (echo failed: %~1& exit /b 1)\r\n"
        "for %%D in (\"%T%\") do icacls \"%%~dpD.\" /restore \"%TEMP%\\wl-icon-acl.txt\" >nul 2>&1\r\n"
        "icacls \"%T%\" /setowner \"NT SERVICE\\TrustedInstaller\" >nul 2>&1\r\n"
        "echo restored: %~1\r\n"
        "exit /b 0\r\n";
    return writeFileAtomic(script, text);
}

} // namespace

Result<std::string> patchIconBytes(const std::string& original, const std::vector<IconReplacement>& replacements) {
    auto before = PeImage::parse(original);
    if (!before) {
        return std::unexpected(before.error());
    }
    if (!before->hasResources()) {
        return fail(ErrorCode::InvalidArgument, L"the file has no resources", L"");
    }
    PeImage after = *before;
    for (const auto& r : replacements) {
        if (auto ok = replaceIconGroup(after.resources(), r.group, r.images); !ok) {
            return std::unexpected(ok.error());
        }
    }
    auto bytes = after.build();
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    // Read back what was written and hold it against the plan.
    auto check = PeImage::parse(*bytes);
    if (!check) {
        return fail(ErrorCode::Unknown, L"the patched file does not parse", check.error().message);
    }
    const auto& oldTree = before->resources();
    const auto& newTree = check->resources();
    for (const auto& type : oldTree.types) {
        if (!type.key.named() && (type.key.id == kRtIcon || type.key.id == kRtGroupIcon)) {
            continue;
        }
        const auto same = std::ranges::find(newTree.types, type.key, &ResourceType::key);
        if (same == newTree.types.end() || !sameLeaves(type, *same)) {
            return fail(ErrorCode::Unknown, L"a resource that was not an icon changed", type.key.text());
        }
    }
    const auto oldGroups = listIconGroups(oldTree);
    const auto newGroups = listIconGroups(newTree);
    if (oldGroups.size() != newGroups.size()) {
        return fail(ErrorCode::Unknown, L"the number of icons changed", L"");
    }
    for (std::size_t i = 0; i < oldGroups.size(); ++i) {
        const auto replaced = std::ranges::find(replacements, oldGroups[i].key, &IconReplacement::group);
        const auto& want = replaced != replacements.end() ? replaced->images : oldGroups[i].images;
        const auto& got = newGroups[i].images;
        if (newGroups[i].key != oldGroups[i].key || want.size() != got.size() ||
            !std::ranges::equal(want, got, [](const IconImage& a, const IconImage& b) { return a.data == b.data; })) {
            return fail(ErrorCode::Unknown, L"an icon came out different", oldGroups[i].key.text());
        }
    }
    return std::move(*bytes);
}

Result<IconLoadCheck> verifyIconFileWithWindows(const std::filesystem::path& file) {
    HMODULE module = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    if (!module) {
        return std::unexpected(win32(L"Windows cannot open the file as a resource module", file));
    }
    std::vector<std::wstring> names; // "#id" or the name
    EnumResourceNamesW(
        module, RT_GROUP_ICON,
        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
            auto* out = reinterpret_cast<std::vector<std::wstring>*>(param);
            out->push_back(IS_INTRESOURCE(name) ? std::format(L"#{}", reinterpret_cast<std::uintptr_t>(name)) : std::wstring(name));
            return TRUE;
        },
        reinterpret_cast<LONG_PTR>(&names));
    IconLoadCheck check;
    check.groups = static_cast<int>(names.size());
    auto failed = [&](const std::wstring& what) {
        ++check.failed;
        if (check.firstFailure.empty()) {
            check.firstFailure = what;
        }
    };
    for (const auto& name : names) {
        const wchar_t* id = name.starts_with(L'#') ? MAKEINTRESOURCEW(std::stoi(name.substr(1))) : name.c_str();
        HRSRC groupRes = FindResourceW(module, id, RT_GROUP_ICON);
        HGLOBAL groupData = groupRes ? LoadResource(module, groupRes) : nullptr;
        const auto* dir = groupData ? static_cast<const std::uint8_t*>(LockResource(groupData)) : nullptr;
        const DWORD dirSize = groupRes ? SizeofResource(module, groupRes) : 0;
        if (!dir || dirSize < 6) {
            failed(name + L": group");
            continue;
        }
        std::uint16_t count = 0;
        std::memcpy(&count, dir + 4, 2);
        if (dirSize < 6u + count * 14u) {
            failed(name + L": group size");
            continue;
        }
        for (std::uint16_t i = 0; i < count; ++i) {
            std::uint16_t iconId = 0;
            std::memcpy(&iconId, dir + 6 + i * 14 + 12, 2);
            ++check.images;
            HRSRC iconRes = FindResourceW(module, MAKEINTRESOURCEW(iconId), RT_ICON);
            HGLOBAL iconData = iconRes ? LoadResource(module, iconRes) : nullptr;
            auto* bits = iconData ? static_cast<BYTE*>(LockResource(iconData)) : nullptr;
            const DWORD size = iconRes ? SizeofResource(module, iconRes) : 0;
            HICON icon = bits ? CreateIconFromResourceEx(bits, size, TRUE, 0x00030000, 0, 0, LR_DEFAULTCOLOR) : nullptr;
            if (!icon) {
                failed(std::format(L"{}: image {} (icon #{})", name, i, iconId));
                continue;
            }
            DestroyIcon(icon);
        }
    }
    FreeLibrary(module);
    return check;
}

Result<void> checkIconPatchPath(std::wstring_view raw) {
    const std::wstring relative = normalized(raw);
    const std::wstring low = lower(relative);
    if (!low.starts_with(L"windows\\") || low.find(L"..") != std::wstring::npos || low.find(L':') != std::wstring::npos) {
        return fail(ErrorCode::InvalidArgument, L"only files under Windows\\ can be patched", relative);
    }
    for (const wchar_t* refused : {L"windows\\winsxs\\", L"windows\\servicing\\", L"windows\\boot\\", L"windows\\winlove\\",
                                   L"windows\\system32\\boot\\", L"windows\\system32\\drivers\\", L"windows\\system32\\config\\",
                                   L"windows\\system32\\catroot", L"windows\\installer\\"}) {
        if (low.starts_with(refused)) {
            return fail(ErrorCode::InvalidArgument, L"this folder belongs to Windows' servicing or boot: not patched", relative);
        }
    }
    const std::wstring ext = lower(std::filesystem::path(relative).extension().wstring());
    if (ext != L".mun" && ext != L".dll" && ext != L".exe" && ext != L".cpl" && ext != L".ocx" && ext != L".scr") {
        return fail(ErrorCode::InvalidArgument, L"not a resource file (.mun, .dll, .exe, .cpl, .ocx, .scr)", relative);
    }
    return {};
}

Result<void> checkIconPatchTarget(std::wstring_view relative, const PeImage& pe) {
    if (auto ok = checkIconPatchPath(relative); !ok) {
        return ok;
    }
    if (pe.hasCode()) {
        return fail(ErrorCode::InvalidArgument,
                    L"the file contains program code: patching it would break its signature (Smart App Control / WDAC "
                    L"could then block it); use the redirect mode for its icons",
                    std::wstring(relative));
    }
    const auto* groups = pe.resources().type(kRtGroupIcon);
    if (!groups || groups->names.empty()) {
        return fail(ErrorCode::InvalidArgument, L"the file has no icons", std::wstring(relative));
    }
    return {};
}

std::wstring iconBackupPath(std::wstring_view raw) {
    const std::wstring relative = normalized(raw);
    const std::wstring underWindows = relative.substr(std::min<std::size_t>(relative.size(), std::wstring_view(L"Windows\\").size()));
    return std::wstring(kBackupRoot) + L"\\" + underWindows;
}

Result<std::wstring> patchImageIcons(const std::filesystem::path& mountDir, std::wstring_view raw,
                                     const std::vector<IconReplacement>& replacements) {
    const std::wstring relative = normalized(raw);
    if (auto ok = checkIconPatchPath(relative); !ok) {
        return std::unexpected(ok.error());
    }
    auto target = resolveImagePath(mountDir, relative);
    auto backup = resolveImagePath(mountDir, iconBackupPath(relative));
    if (!target) {
        return std::unexpected(target.error());
    }
    if (!backup) {
        return std::unexpected(backup.error());
    }
    enableBackupRestore();
    // Step 2: the file as it is is the base; the first patch keeps the original. A backup next to a
    // file servicing owns again (an update replaced our patch) is of an older build: the new file
    // is the original now.
    bool hasBackup = std::filesystem::is_regular_file(*backup);
    if (hasBackup && linkCount(*target) > 1) {
        log::info("icons", L"an update replaced the patched file; its new version becomes the backup: " + relative);
        hasBackup = false;
    }
    auto base = readProtected(*target);
    if (!base) {
        return std::unexpected(base.error());
    }
    auto pe = PeImage::parse(*base);
    if (!pe) {
        return std::unexpected(pe.error());
    }
    if (auto ok = checkIconPatchTarget(relative, *pe); !ok) {
        return std::unexpected(ok.error());
    }
    auto patched = patchIconBytes(*base, replacements);
    if (!patched) {
        return std::unexpected(patched.error());
    }
    if (!hasBackup) {
        std::error_code ec;
        std::filesystem::create_directories(backup->parent_path(), ec);
        if (auto saved = writeFileAtomic(*backup, *base); !saved) {
            return std::unexpected(saved.error());
        }
    }
    auto security = securityOf(*target);
    if (!security) {
        return std::unexpected(security.error());
    }
    if (auto written = writeReplacing(*target, *patched, *security, verifyIconFileWithWindows, *base); !written) {
        return std::unexpected(written.error());
    }
    if (auto script = writeRestoreScript(mountDir); !script) {
        log::warn("icons", L"restore script not written: " + script.error().message);
    }
    log::info("icons", std::format(L"patched {} ({} icon(s)); original in {}", relative, replacements.size(),
                                   iconBackupPath(relative)));
    return iconBackupPath(relative);
}

Result<void> restoreImageIcons(const std::filesystem::path& mountDir, std::wstring_view raw) {
    const std::wstring relative = normalized(raw);
    if (auto ok = checkIconPatchPath(relative); !ok) {
        return ok;
    }
    auto target = resolveImagePath(mountDir, relative);
    auto backup = resolveImagePath(mountDir, iconBackupPath(relative));
    if (!target || !backup) {
        return std::unexpected(!target ? target.error() : backup.error());
    }
    std::error_code ec;
    if (linkCount(*target) > 1) {
        // Servicing owns the file again (an update replaced the patch): it already is Microsoft's,
        // and the backup is of an older build — putting it back would downgrade the file.
        std::filesystem::remove(*backup, ec);
        if (auto script = writeRestoreScript(mountDir); !script) {
            log::warn("icons", L"restore script not written: " + script.error().message);
        }
        log::info("icons", L"already Microsoft's (updated): " + relative);
        return {};
    }
    auto original = readProtected(*backup);
    if (!original) {
        return std::unexpected(original.error());
    }
    auto security = securityOf(*target);
    if (!security) {
        return std::unexpected(security.error());
    }
    if (auto written = writeReplacing(*target, *original, *security, nullptr, *original); !written) {
        return written;
    }
    std::filesystem::remove(*backup, ec);
    if (auto script = writeRestoreScript(mountDir); !script) {
        log::warn("icons", L"restore script not written: " + script.error().message);
    }
    log::info("icons", L"restored " + relative);
    return {};
}

std::vector<std::wstring> patchedIconFiles(const std::filesystem::path& mountDir) {
    std::vector<std::wstring> out;
    auto root = resolveImagePath(mountDir, kBackupRoot);
    std::error_code ec;
    if (!root || !std::filesystem::is_directory(*root, ec)) {
        return out;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(*root, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().filename() == kRestoreScript) {
            continue;
        }
        const auto rel = std::filesystem::relative(it->path(), *root, ec);
        if (!ec) {
            out.push_back(L"Windows\\" + rel.wstring());
        }
    }
    std::ranges::sort(out);
    return out;
}

ResourceKey resourceKeyFromText(std::wstring_view text) {
    if (text.size() > 1 && text.size() <= 6 && text.front() == L'#' &&
        std::all_of(text.begin() + 1, text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        const unsigned long v = std::wcstoul(std::wstring(text.substr(1)).c_str(), nullptr, 10);
        if (v <= 0xFFFF) {
            return ResourceKey{static_cast<std::uint16_t>(v), {}};
        }
    }
    return ResourceKey{0, std::wstring(text)};
}

std::wstring iconPatchValue(const IconPatchRequest& request) {
    nlohmann::ordered_json j;
    if (request.restore) {
        j["restore"] = true;
    } else {
        nlohmann::ordered_json groups = nlohmann::ordered_json::object();
        for (const auto& [key, source] : request.groups) {
            groups[utf8::fromWide(key.text())] = utf8::fromWide(source.wstring());
        }
        j["groups"] = std::move(groups);
    }
    return utf8::toWide(j.dump());
}

Result<IconPatchRequest> iconPatchRequest(std::wstring_view value) {
    const auto j = nlohmann::json::parse(utf8::fromWide(value), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return fail(ErrorCode::ParseError, L"bad icon patch value", std::wstring(value));
    }
    IconPatchRequest request;
    if (j.value("restore", false)) {
        request.restore = true;
        return request;
    }
    const auto groups = j.find("groups");
    if (groups == j.end() || !groups->is_object() || groups->empty()) {
        return fail(ErrorCode::ParseError, L"an icon patch needs groups", std::wstring(value));
    }
    for (const auto& [key, source] : groups->items()) {
        if (!source.is_string() || key.empty()) {
            return fail(ErrorCode::ParseError, L"bad icon patch group", std::wstring(value));
        }
        request.groups.emplace_back(resourceKeyFromText(utf8::toWide(key)), utf8::toWide(source.get<std::string>()));
    }
    return request;
}

Result<void> applyIconPatch(const std::filesystem::path& mountDir, std::wstring_view relative, const IconPatchRequest& request) {
    if (request.restore) {
        return restoreImageIcons(mountDir, relative);
    }
    std::vector<IconReplacement> replacements;
    for (const auto& [key, source] : request.groups) {
        auto images = loadIconSource(source);
        if (!images) {
            return std::unexpected(images.error());
        }
        replacements.push_back({key, std::move(*images)});
    }
    auto patched = patchImageIcons(mountDir, relative, replacements);
    if (!patched) {
        return std::unexpected(patched.error());
    }
    return {};
}

} // namespace wl::core
