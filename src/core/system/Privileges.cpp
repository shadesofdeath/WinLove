#include "core/system/Privileges.h"

#include <windows.h>
#include <shellapi.h>

namespace wl::core {

bool isElevated() noexcept {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                          elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

Result<void> enablePrivilege(const wchar_t* name) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return fail(ErrorCode::AccessDenied, L"OpenProcessToken failed", name,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid)) {
        CloseHandle(token);
        return fail(ErrorCode::InvalidArgument, L"unknown privilege", name,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    // AdjustTokenPrivileges "succeeds" without the privilege: ERROR_NOT_ALL_ASSIGNED tells the truth.
    const DWORD status = GetLastError();
    CloseHandle(token);
    if (status != ERROR_SUCCESS) {
        return fail(ErrorCode::AccessDenied, L"privilege not held (run elevated)", name,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(status)));
    }
    return {};
}

std::wstring quoteArgument(std::wstring_view argument) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

Result<void> relaunchElevated(std::wstring_view arguments) {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring args(arguments);
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = args.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            return fail(ErrorCode::Cancelled, L"elevation declined", L"UAC");
        }
        return fail(ErrorCode::AccessDenied, L"could not relaunch elevated", exe,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(error)));
    }
    return {};
}

} // namespace wl::core
