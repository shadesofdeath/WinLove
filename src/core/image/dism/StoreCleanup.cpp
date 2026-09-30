#include "core/image/dism/StoreCleanup.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <json.hpp>

#include <windows.h>

#include <cctype>
#include <charconv>
#include <format>
#include <functional>

namespace wl::core {

namespace {

using Json = nlohmann::json;

// Runs `commandLine` hidden, feeding everything it prints to `onOutput`. Returns the exit code.
Result<DWORD> runCaptured(std::wstring commandLine, const std::function<void(std::string_view)>& onOutput) {
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
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &startup, &process);
    const DWORD startError = GetLastError();
    CloseHandle(writeEnd); // ours: the read below ends when the child's copy closes
    if (!started) {
        CloseHandle(readEnd);
        return fail(ErrorCode::IoError, L"could not start dism.exe", commandLine,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(startError)));
    }
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        onOutput(std::string_view(buffer, read));
    }
    CloseHandle(readEnd);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}

} // namespace

std::string storeCleanupToJson(const StoreCleanupOptions& options) {
    return Json{{"title", utf8::fromWide(options.title)}, {"resetBase", options.resetBase}}.dump();
}

Result<StoreCleanupOptions> storeCleanupFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"not a cleanup operation");
    }
    StoreCleanupOptions options;
    try {
        options.title = utf8::toWide(doc.value("title", std::string{}));
        options.resetBase = doc.value("resetBase", true);
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed cleanup operation", utf8::toWide(e.what()));
    }
    return options;
}

std::wstring storeCleanupCommandLine(const std::filesystem::path& dismExe, const std::filesystem::path& mountDir,
                                     bool resetBase) {
    // A trailing backslash would escape the closing quote ("C:\m\" → C:\m").
    std::wstring image = mountDir.wstring();
    while (image.size() > 3 && (image.back() == L'\\' || image.back() == L'/')) {
        image.pop_back();
    }
    // /English: the progress line and the error text are parsed / logged.
    std::wstring line = std::format(L"\"{}\" /English /Image:\"{}\" /Cleanup-Image /StartComponentCleanup",
                                    dismExe.wstring(), image);
    if (resetBase) {
        line += L" /ResetBase";
    }
    return line;
}

std::optional<double> lastDismPercent(std::string_view output) {
    for (std::size_t at = output.rfind('%'); at != std::string_view::npos && at > 0;
         at = at == 0 ? std::string_view::npos : output.rfind('%', at - 1)) {
        std::size_t start = at;
        while (start > 0 && (std::isdigit(static_cast<unsigned char>(output[start - 1])) || output[start - 1] == '.')) {
            --start;
        }
        if (start == at) {
            continue;
        }
        double value = 0;
        const auto parsed = std::from_chars(output.data() + start, output.data() + at, value);
        if (parsed.ec == std::errc{} && value >= 0 && value <= 100) {
            return value / 100.0;
        }
    }
    return std::nullopt;
}

Result<void> cleanupComponentStore(DismSession& session, bool resetBase, const TaskContext& task) {
    if (auto go = task.cancel.check(L"component store cleanup"); !go) {
        return go;
    }
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return fail(ErrorCode::IoError, L"could not find the system folder");
    }
    const std::wstring line =
        storeCleanupCommandLine(std::filesystem::path(system) / L"dism.exe", session.mountPath(), resetBase);
    log::info("dism", line);

    // dism.exe opens its own session on the image: ours must not be in the way.
    session.suspend();
    std::string tail; // the end of the output: DISM's own words when it fails
    auto exit = runCaptured(line, [&](std::string_view chunk) {
        if (const auto percent = lastDismPercent(chunk)) {
            task.report(*percent, L"StartComponentCleanup");
        }
        tail.append(chunk);
        if (tail.size() > 8192) {
            tail.erase(0, tail.size() - 4096);
        }
    });
    auto reopened = session.reload();
    if (!exit) {
        return std::unexpected(exit.error());
    }
    // What DISM said, without the progress bar redraws.
    std::wstring message;
    std::wstring current;
    for (const char c : tail) {
        if (c == '\r' || c == '\n') {
            if (!current.empty() && current.find(L"[=") == std::wstring::npos && current.find(L"%") == std::wstring::npos) {
                log::debug("dism", current);
                if (current.starts_with(L"Error") || !message.empty()) {
                    message += (message.empty() ? L"" : L" ") + current;
                }
            }
            current.clear();
        } else if (c != '\0') {
            current.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
    }
    if (*exit == 0x800F0806) { // CBS_E_PENDING
        return fail(ErrorCode::Unsupported,
                    L"component store cleanup skipped: the image has pending operations (a feature or update of this "
                    L"run finishes at first boot); the image itself is unchanged by this step",
                    message, static_cast<std::int32_t>(*exit));
    }
    if (*exit != 0) {
        return fail(ErrorCode::IoError, L"component store cleanup failed",
                    message.empty() ? std::format(L"dism.exe exit code 0x{:08X}", *exit) : message,
                    static_cast<std::int32_t>(*exit));
    }
    if (!reopened) {
        return reopened;
    }
    task.report(1.0, L"StartComponentCleanup");
    return {};
}

} // namespace wl::core
