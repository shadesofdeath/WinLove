// wlcli — drives the image engine without UI (docs/ENGINE.md §4).
// Every engine capability gets a command here before any page uses it.
#include "core/image/ImageFormat.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

void print(std::wstring_view text) {
    const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(out, &mode)) {
        DWORD written = 0;
        ::WriteConsoleW(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        // Redirected (pipe/file): emit UTF-8 so tests and tools can parse it.
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                               nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<std::size_t>(size), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size,
                              nullptr, nullptr);
        std::fwrite(utf8.data(), 1, utf8.size(), stdout);
    }
}

void printUsage() {
    print(L"wlcli " WL_VERSION_STRING L" - WinLove image engine CLI\n"
          L"\n"
          L"Usage:\n"
          L"  wlcli version            Print version\n"
          L"  wlcli format <path>      Classify a source file by extension\n"
          L"  wlcli help               Show this help\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    const std::wstring_view command = argv[1];
    if (command == L"version") {
        print(L"" WL_VERSION_STRING L"\n");
        return 0;
    }
    if (command == L"format" && argc == 3) {
        const auto format = wl::core::formatFromPath(argv[2]);
        print(std::wstring(wl::core::formatName(format)) + L"\n");
        return format == wl::core::ImageFormat::Unknown ? 2 : 0;
    }
    if (command == L"help" || command == L"--help" || command == L"-h") {
        printUsage();
        return 0;
    }
    print(L"unknown command: " + std::wstring(command) + L"\n\n");
    printUsage();
    return 1;
}
