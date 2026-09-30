#include "core/system/Process.h"

#include <windows.h>

namespace wl::core {

Result<std::uint32_t> runProcess(std::wstring commandLine, const std::function<void(std::string_view)>& onOutput) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
        return fail(ErrorCode::IoError, L"could not create a pipe", {}, static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0); // only the write end goes to the child
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    startup.hStdInput = nullptr; // a tool that asks something reads end-of-file instead of waiting
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &startup, &process);
    const DWORD startError = GetLastError();
    CloseHandle(writeEnd); // ours: the read below ends when the child's copy closes
    if (!started) {
        CloseHandle(readEnd);
        return fail(ErrorCode::IoError, L"could not start the program", commandLine,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(startError)));
    }
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        if (onOutput) {
            onOutput(std::string_view(buffer, read));
        }
    }
    CloseHandle(readEnd);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<std::uint32_t>(exitCode);
}

Result<std::wstring> systemTool(std::wstring_view name) {
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return fail(ErrorCode::IoError, L"could not find the system folder");
    }
    return std::wstring(system) + L"\\" + std::wstring(name);
}

} // namespace wl::core
