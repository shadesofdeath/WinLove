// WinLove.exe entry point. See app/App.h for the command line.
#include "app/App.h"

#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*commandLine*/, int /*show*/) {
    // STA: WIC, drag & drop and shell dialogs need it.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        return 1;
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(argv + 1, argv + argc);
    LocalFree(argv);

    int exitCode = 1;
    if (auto options = wl::app::parseLaunchOptions(args)) {
        wl::app::App app(std::move(*options));
        exitCode = app.run();
    } else {
        const std::wstring text = L"WinLove: " + wl::describe(options.error()) + L"\n";
        // Headless runs (--render) must never block on a dialog: tests and AI sessions drive them.
        const bool headless =
            std::ranges::any_of(args, [](const std::wstring& a) { return a.starts_with(L"--render"); });
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
    CoUninitialize();
    return exitCode;
}
