#include "app/CrashReport.h"

#include <windows.h>

#include <dbghelp.h>
#include <shlobj.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>

namespace wl::app {

namespace {

// Where the reports go, worked out once at start: the handler runs in a process that is already
// broken and should not need the heap for it (audit APP-19). Empty: the working directory.
wchar_t g_folder[MAX_PATH]{};

// What the C++ runtime ends the process for without raising an exception of its own; the numbers
// only name the cause in the report ("E" + "WL" + n: the customer bit set, as RaiseException wants).
constexpr DWORD kTerminateCode = 0xE0574C01;        // std::terminate: noexcept broken, a destructor threw
constexpr DWORD kPureCallCode = 0xE0574C02;         // pure virtual call
constexpr DWORD kInvalidParameterCode = 0xE0574C03; // the CRT refused an argument
constexpr DWORD kAbortCode = 0xE0574C04;            // abort()

void rememberFolder() {
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        const std::wstring folder = (std::filesystem::path(local) / L"WinLove" / L"logs").wstring();
        if (folder.size() < MAX_PATH - 40) { // room for "\crash-YYYYMMDD-HHMMSS.dmp"
            wcsncpy_s(g_folder, folder.c_str(), _TRUNCATE);
        }
    }
    CoTaskMemFree(local);
}

void line(FILE* out, const wchar_t* text) {
    std::fputws(text, out);
    std::fputws(L"\n", out);
}

const wchar_t* fileNameOf(const wchar_t* path) {
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    return slash ? slash + 1 : path;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t base[MAX_PATH + 64]{};
    if (g_folder[0] != L'\0') {
        // The log folder may not exist yet (a crash before the first log line): its parent, then it.
        wchar_t parent[MAX_PATH]{};
        wcsncpy_s(parent, g_folder, _TRUNCATE);
        if (wchar_t* slash = std::wcsrchr(parent, L'\\')) {
            *slash = L'\0';
            CreateDirectoryW(parent, nullptr);
        }
        CreateDirectoryW(g_folder, nullptr);
        std::swprintf(base, MAX_PATH + 64, L"%s\\crash-%04u%02u%02u-%02u%02u%02u", g_folder, now.wYear, now.wMonth, now.wDay,
                      now.wHour, now.wMinute, now.wSecond);
    } else {
        std::swprintf(base, MAX_PATH + 64, L"crash-%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
                      now.wMinute, now.wSecond);
    }
    wchar_t file[MAX_PATH + 72]{};

    HANDLE process = GetCurrentProcess();
    std::swprintf(file, MAX_PATH + 72, L"%s.dmp", base);
    if (const HANDLE dump = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(process, GetCurrentProcessId(), dump, MiniDumpWithThreadInfo, &exception, nullptr, nullptr);
        CloseHandle(dump);
    }

    FILE* out = nullptr;
    std::swprintf(file, MAX_PATH + 72, L"%s.txt", base);
    if (_wfopen_s(&out, file, L"w, ccs=UTF-8") != 0 || !out) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    wchar_t text[1024];
    const auto* record = info->ExceptionRecord;
    std::swprintf(text, 1024, L"WinLove crash: exception 0x%08lX at %p", record->ExceptionCode, record->ExceptionAddress);
    line(out, text);
    switch (record->ExceptionCode) {
    case kTerminateCode: line(out, L"std::terminate (an exception nothing could catch: noexcept, a destructor)"); break;
    case kPureCallCode: line(out, L"pure virtual function call"); break;
    case kInvalidParameterCode: line(out, L"invalid parameter passed to a C runtime function"); break;
    case kAbortCode: line(out, L"abort()"); break;
    default: break;
    }
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
        std::swprintf(text, 1024, L"  %2d  %s+0x%llX  %s  %s:%lu", depth, fileNameOf(module),
                      static_cast<unsigned long long>(address - moduleBase), named ? symbol->Name : L"?",
                      hasLine ? source.FileName : L"", hasLine ? source.LineNumber : 0UL);
        line(out, text);
    }
    SymCleanup(process);
    std::fclose(out);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The same report for an end that is not an unhandled exception: written from here, with this
// thread's state as it is now — raising an exception inside a terminate handler would run the
// handlers of the frames it is being called from.
void reportFatal(DWORD code) {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = code;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = reinterpret_cast<PVOID>(context.Rip);
    EXCEPTION_POINTERS pointers{&record, &context};
    onCrash(&pointers);
}

[[noreturn]] void onTerminate() {
    reportFatal(kTerminateCode);
    std::abort(); // what the default handler does; the report is written only once
}

void onPureCall() {
    reportFatal(kPureCallCode);
    std::abort();
}

void onInvalidParameter(const wchar_t* /*expression*/, const wchar_t* /*function*/, const wchar_t* /*file*/,
                        unsigned int /*line*/, uintptr_t /*reserved*/) {
    reportFatal(kInvalidParameterCode);
    std::abort(); // as without a handler: the process does not go on with the bad call
}

void onAbort(int /*signal*/) {
    reportFatal(kAbortCode); // abort() ends the process when this returns
}

} // namespace

void installCrashReport() {
    rememberFolder();
    SetUnhandledExceptionFilter(onCrash);
    // What never reaches that filter (audit APP-19): a broken noexcept or a throwing destructor
    // (std::terminate → abort), a pure virtual call, an argument the CRT refuses. The terminate
    // handler is per thread in this runtime — this is the UI thread's; the worker threads' ends
    // come through abort(), whose signal handler is the process's.
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    std::signal(SIGABRT, onAbort);
}

} // namespace wl::app
