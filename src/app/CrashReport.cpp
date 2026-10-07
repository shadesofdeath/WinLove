#include "app/CrashReport.h"

#include <windows.h>

#include <dbghelp.h>
#include <shlobj.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>

namespace wl::app {

namespace {

std::filesystem::path crashFolder() {
    PWSTR local = nullptr;
    std::filesystem::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        folder = std::filesystem::path(local) / L"WinLove" / L"logs";
    }
    CoTaskMemFree(local);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    return folder;
}

void line(FILE* out, const wchar_t* text) {
    std::fputws(text, out);
    std::fputws(L"\n", out);
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[32];
    std::swprintf(stamp, 32, L"%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const auto folder = crashFolder();
    const auto base = folder / (std::wstring(L"crash-") + stamp);

    HANDLE process = GetCurrentProcess();
    if (const HANDLE dump = CreateFileW((base.wstring() + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(process, GetCurrentProcessId(), dump, MiniDumpWithThreadInfo, &exception, nullptr, nullptr);
        CloseHandle(dump);
    }

    FILE* out = nullptr;
    if (_wfopen_s(&out, (base.wstring() + L".txt").c_str(), L"w, ccs=UTF-8") != 0 || !out) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    wchar_t text[1024];
    const auto* record = info->ExceptionRecord;
    std::swprintf(text, 1024, L"WinLove crash: exception 0x%08lX at %p", record->ExceptionCode, record->ExceptionAddress);
    line(out, text);
    line(out, GetCommandLineW());

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitializeW(process, nullptr, TRUE);
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFOW) BYTE symbolBuffer[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(symbolBuffer);
    for (int depth = 0; depth < 64; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        const DWORD64 address = frame.AddrPC.Offset;
        wchar_t module[MAX_PATH] = L"?";
        const DWORD64 moduleBase = SymGetModuleBase64(process, address);
        if (moduleBase) {
            GetModuleFileNameW(reinterpret_cast<HMODULE>(moduleBase), module, MAX_PATH);
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
        symbol->MaxNameLen = 512;
        DWORD64 displacement = 0;
        const bool named = SymFromAddrW(process, address, &displacement, symbol) != 0;
        IMAGEHLP_LINEW64 source{sizeof(IMAGEHLP_LINEW64)};
        DWORD lineDisplacement = 0;
        const bool hasLine = SymGetLineFromAddrW64(process, address, &lineDisplacement, &source) != 0;
        std::swprintf(text, 1024, L"  %2d  %s+0x%llX  %s  %s:%lu", depth, std::filesystem::path(module).filename().c_str(),
                      static_cast<unsigned long long>(address - moduleBase), named ? symbol->Name : L"?",
                      hasLine ? source.FileName : L"", hasLine ? source.LineNumber : 0UL);
        line(out, text);
    }
    SymCleanup(process);
    std::fclose(out);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installCrashReport() {
    SetUnhandledExceptionFilter(onCrash);
}

} // namespace wl::app
