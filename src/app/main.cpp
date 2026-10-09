// WinLove.exe entry point. See app/App.h for the command line.
#include "app/App.h"
#include "app/CrashReport.h"
#include "core/system/Privileges.h"

#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace {

// One WinLove per Windows session (and per --profile, for test runs with their own files): two
// would take over the same mount and could unmount it under each other (audit A12). The name is
// shared by elevated and unelevated processes of the session.
std::wstring instanceName(const std::vector<std::wstring>& args) {
    std::wstring name = LR"(Local\WinLove.Instance)";
    for (const auto& a : args) {
        if (a.starts_with(L"--profile=")) {
            std::wstring folder = a.substr(10);
            for (auto& c : folder) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            name += std::format(L".{:016x}", std::hash<std::wstring>{}(folder));
        }
    }
    return name;
}

bool anotherInstance(const std::wstring& name) {
    if (HANDLE other = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str())) {
        CloseHandle(other);
        return true;
    }
    return GetLastError() == ERROR_ACCESS_DENIED; // an elevated instance's: there, not ours to open
}

void showOtherInstance() {
    if (HWND other = FindWindowW(L"WinLove.Window", nullptr)) {
        if (IsIconic(other)) {
            ShowWindowAsync(other, SW_RESTORE);
        }
        SetForegroundWindow(other);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*commandLine*/, int /*show*/) {
    wl::app::installCrashReport();
    // OLE (STA): drag & drop needs OleInitialize, WIC and shell dialogs need COM.
    if (FAILED(OleInitialize(nullptr))) {
        return 1;
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(argv + 1, argv + argc);
    LocalFree(argv);

    // Mount/unmount/servicing need admin: start elevated right away (one UAC prompt) instead of
    // asking in-app later. Headless --render runs stay as they are (tests, AI sessions). If the
    // user declines UAC we keep running unelevated; operations then show the s4 dialog.
    const bool render = std::ranges::any_of(args, [](const std::wstring& a) { return a.starts_with(L"--render"); });
    const bool noElevate = std::ranges::find(args, std::wstring(L"--no-elevate")) != args.end();
    // Renders (tests, AI sessions) never count as an instance.
    const std::wstring instance = instanceName(args);
    // Started by a WinLove that relaunches itself elevated: that one closes in a moment.
    const bool relaunched = std::ranges::find(args, std::wstring(L"--relaunched")) != args.end();
    for (int wait = 0; relaunched && !render && wait < 100 && anotherInstance(instance); ++wait) {
        Sleep(100);
    }
    if (!render && anotherInstance(instance)) {
        showOtherInstance(); // before any UAC prompt: the one already open comes to the front
        OleUninitialize();
        return 0;
    }
    if (!render && !noElevate && !wl::core::isElevated()) {
        std::wstring joined;
        for (const auto& a : args) {
            joined += (joined.empty() ? L"" : L" ") + wl::core::quoteArgument(a);
        }
        // Never relaunch twice (e.g. UAC disabled for a standard user starts the child unelevated).
        joined += joined.empty() ? L"--no-elevate" : L" --no-elevate";
        if (wl::core::relaunchElevated(joined)) {
            OleUninitialize();
            return 0;
        }
    }

    HANDLE instanceMutex = nullptr;
    if (!render) {
        instanceMutex = CreateMutexW(nullptr, TRUE, instance.c_str());
        if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) { // started at the same moment
            CloseHandle(instanceMutex);
            showOtherInstance();
            OleUninitialize();
            return 0;
        }
    }

    int exitCode = 1;
    if (auto options = wl::app::parseLaunchOptions(args)) {
        wl::app::App app(std::move(*options));
        exitCode = app.run();
    } else {
        const std::wstring text = L"WinLove: " + wl::describe(options.error()) + L"\n";
        // Headless runs (--render) must never block on a dialog: tests and AI sessions drive them.
        const bool headless = render;
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            DWORD written = 0;
            WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), text.c_str(), static_cast<DWORD>(text.size()), &written,
                          nullptr);
            FreeConsole();
        }
        if (!headless) {
            MessageBoxW(nullptr, text.c_str(), L"WinLove", MB_OK | MB_ICONERROR);
        }
    }
    if (instanceMutex) {
        CloseHandle(instanceMutex);
    }
    OleUninitialize();
    return exitCode;
}
