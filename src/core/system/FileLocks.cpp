#include "core/system/FileLocks.h"

#include "base/Log.h"
#include "core/system/Privileges.h"

#include <aclapi.h>
#include <ole2.h> // WIN32_LEAN_AND_MEAN drops OLE; exdisp.h needs it
#include <exdisp.h>
#include <restartmanager.h>
#include <shlwapi.h>

#include <format>

namespace wl::core {

namespace {

// Case-insensitive "is `path` equal to or inside `folder`".
bool isInside(const std::filesystem::path& path, const std::filesystem::path& folder) {
    std::wstring p = path.lexically_normal().wstring();
    std::wstring f = folder.lexically_normal().wstring();
    while (f.size() > 3 && (f.back() == L'\\' || f.back() == L'/')) {
        f.pop_back();
    }
    if (p.size() < f.size() ||
        CompareStringOrdinal(p.data(), static_cast<int>(f.size()), f.data(), static_cast<int>(f.size()), TRUE) !=
            CSTR_EQUAL) {
        return false;
    }
    return p.size() == f.size() || p[f.size()] == L'\\' || p[f.size()] == L'/';
}

// COM for the calling thread (the engine thread has none); undone on scope exit if we did it.
struct ComScope {
    bool owned = false;
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owned = SUCCEEDED(hr);
    }
    ~ComScope() {
        if (owned) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

// Calls fn(IWebBrowser2*, folderPath) for every shell window showing a file-system folder.
template <class F>
void forEachExplorerWindow(F&& fn) {
    ComScope com;
    IShellWindows* windows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) {
        return;
    }
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = i;
        IDispatch* dispatch = nullptr;
        if (windows->Item(index, &dispatch) != S_OK || !dispatch) {
            continue;
        }
        IWebBrowser2* browser = nullptr;
        if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&browser)))) {
            BSTR url = nullptr;
            if (SUCCEEDED(browser->get_LocationURL(&url)) && url && SysStringLen(url) > 0) {
                wchar_t path[MAX_PATH * 4]{};
                DWORD length = static_cast<DWORD>(std::size(path));
                if (SUCCEEDED(PathCreateFromUrlW(url, path, &length, 0))) {
                    fn(browser, std::filesystem::path(path));
                }
            }
            SysFreeString(url);
            browser->Release();
        }
        dispatch->Release();
    }
    windows->Release();
}

bool isReparsePoint(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// Owner := Administrators, DACL := Administrators full control (inherited entries kept off).
bool takeOwnership(const std::filesystem::path& path) {
    (void)enablePrivilege(SE_TAKE_OWNERSHIP_NAME);
    (void)enablePrivilege(SE_RESTORE_NAME);
    (void)enablePrivilege(SE_BACKUP_NAME);
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD sidSize = sizeof(sidBuffer);
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sidBuffer, &sidSize)) {
        return false;
    }
    PSID admins = sidBuffer;
    std::wstring name = path.wstring();
    if (SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, admins, nullptr, nullptr,
                              nullptr) != ERROR_SUCCESS) {
        return false;
    }
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    access.Trustee.ptstrName = static_cast<LPWSTR>(admins);
    PACL acl = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) {
        return false;
    }
    const bool ok = SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, acl,
                                          nullptr) == ERROR_SUCCESS;
    LocalFree(acl);
    return ok;
}

// Removes one entry; directories recursively (never through a reparse point).
DWORD removeEntry(const std::filesystem::path& path) {
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return ERROR_SUCCESS; // already gone
    }
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    auto attempt = [&]() -> DWORD {
        if (directory) {
            if (!isReparsePoint(path)) {
                std::error_code ec;
                for (const auto& child : std::filesystem::directory_iterator(path, ec)) {
                    if (const DWORD status = removeEntry(child.path()); status != ERROR_SUCCESS) {
                        return status;
                    }
                }
                if (ec) {
                    return static_cast<DWORD>(ec.value());
                }
            }
            return RemoveDirectoryW(path.c_str()) ? ERROR_SUCCESS : GetLastError();
        }
        return DeleteFileW(path.c_str()) ? ERROR_SUCCESS : GetLastError();
    };
    DWORD status = attempt();
    if (status == ERROR_ACCESS_DENIED && takeOwnership(path)) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        status = attempt();
    }
    return status;
}

} // namespace

std::vector<FolderBlocker> explorerWindowsIn(const std::filesystem::path& folder) {
    std::vector<FolderBlocker> result;
    forEachExplorerWindow([&](IWebBrowser2* browser, const std::filesystem::path& path) {
        if (isInside(path, folder)) {
            SHANDLE_PTR hwnd = 0;
            browser->get_HWND(&hwnd);
            DWORD pid = 0;
            GetWindowThreadProcessId(reinterpret_cast<HWND>(hwnd), &pid);
            result.push_back({FolderBlocker::Kind::ExplorerWindow, L"explorer.exe", pid, path});
        }
    });
    return result;
}

int releaseExplorerWindows(const std::filesystem::path& folder) {
    int moved = 0;
    const std::wstring target = folder.parent_path().wstring();
    forEachExplorerWindow([&](IWebBrowser2* browser, const std::filesystem::path& path) {
        if (!isInside(path, folder)) {
            return;
        }
        VARIANT url{};
        url.vt = VT_BSTR;
        url.bstrVal = SysAllocString(target.c_str());
        VARIANT empty{};
        if (SUCCEEDED(browser->Navigate2(&url, &empty, &empty, &empty, &empty))) {
            ++moved;
            log::info("locks", L"moved Explorer window away from " + path.wstring());
        }
        VariantClear(&url);
    });
    if (moved > 0) {
        Sleep(400); // Explorer releases its directory handles asynchronously
    }
    return moved;
}

std::vector<FolderBlocker> processesUsing(const std::vector<std::filesystem::path>& files) {
    std::vector<FolderBlocker> result;
    if (files.empty()) {
        return result;
    }
    DWORD session = 0;
    wchar_t key[CCH_RM_SESSION_KEY + 1]{};
    if (RmStartSession(&session, 0, key) != ERROR_SUCCESS) {
        return result;
    }
    std::vector<std::wstring> names;
    names.reserve(files.size());
    for (const auto& f : files) {
        names.push_back(f.wstring());
    }
    std::vector<LPCWSTR> pointers;
    pointers.reserve(names.size());
    for (const auto& n : names) {
        pointers.push_back(n.c_str());
    }
    if (RmRegisterResources(session, static_cast<UINT>(pointers.size()), pointers.data(), 0, nullptr, 0, nullptr) ==
        ERROR_SUCCESS) {
        UINT needed = 0;
        UINT count = 0;
        DWORD reasons = 0;
        DWORD status = RmGetList(session, &needed, &count, nullptr, &reasons);
        if (status == ERROR_MORE_DATA && needed > 0) {
            std::vector<RM_PROCESS_INFO> info(needed);
            count = needed;
            status = RmGetList(session, &needed, &count, info.data(), &reasons);
            if (status == ERROR_SUCCESS) {
                for (UINT i = 0; i < count; ++i) {
                    result.push_back({FolderBlocker::Kind::Process, info[i].strAppName, info[i].Process.dwProcessId, {}});
                }
            }
        }
    }
    RmEndSession(session);
    return result;
}

std::vector<FolderBlocker> blockersOf(const std::filesystem::path& folder) {
    auto result = explorerWindowsIn(folder);
    // Files in the first two levels plus the registry hives: cheap, and where editors and
    // regedit usually hold things.
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             folder, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator() && files.size() < 2000; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (it.depth() >= 2 || isReparsePoint(it->path())) {
            it.disable_recursion_pending();
        }
        if (it->is_regular_file(ec)) {
            files.push_back(it->path());
        }
    }
    const auto config = folder / L"Windows" / L"System32" / L"config";
    for (const wchar_t* hive : {L"SOFTWARE", L"SYSTEM", L"DEFAULT", L"SAM", L"SECURITY", L"COMPONENTS", L"DRIVERS"}) {
        if (std::filesystem::exists(config / hive, ec)) {
            files.push_back(config / hive);
        }
    }
    for (auto& p : processesUsing(files)) {
        result.push_back(std::move(p));
    }
    return result;
}

Result<void> forceRemoveContents(const std::filesystem::path& folder) {
    // Only dedicated folders at least two levels deep (C:\WinLove\mount), never C:\ or C:\WinLove.
    const auto relative = folder.lexically_normal().relative_path();
    int depth = 0;
    for (const auto& part : relative) {
        if (!part.empty()) {
            ++depth;
        }
    }
    if (!folder.is_absolute() || depth < 2) {
        return fail(ErrorCode::InvalidArgument, L"refusing to clear a folder this close to the drive root",
                    folder.wstring());
    }
    if (isReparsePoint(folder)) {
        return fail(ErrorCode::InvalidArgument, L"mount folder is a reparse point", folder.wstring());
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return {};
    }
    releaseExplorerWindows(folder);
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (const DWORD status = removeEntry(entry.path()); status != ERROR_SUCCESS) {
            return fail(ErrorCode::AccessDenied, L"could not remove a leftover in the mount folder (still in use?)",
                        entry.path().wstring(), static_cast<std::int32_t>(HRESULT_FROM_WIN32(status)));
        }
    }
    if (ec) {
        return fail(ErrorCode::IoError, L"could not list the mount folder", folder.wstring(),
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ec.value())));
    }
    return {};
}

} // namespace wl::core
