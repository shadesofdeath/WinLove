// WinLove.exe entry point. See app/App.h for the command line.
#include "app/App.h"
#include "core/system/Privileges.h"

#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*commandLine*/, int /*show*/) {
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
    OleUninitialize();
    return exitCode;
}
