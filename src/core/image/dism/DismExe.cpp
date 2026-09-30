#include "core/image/dism/DismExe.h"

#include "base/Log.h"
#include "core/image/dism/Dism.h"
#include "core/system/Process.h"

#include <windows.h>

#include <cctype>
#include <charconv>
#include <format>

namespace wl::core {

Result<std::filesystem::path> dismExePath() {
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return fail(ErrorCode::IoError, L"could not find the system folder");
    }
    return std::filesystem::path(system) / L"dism.exe";
}

std::wstring dismExeCommandLine(const std::filesystem::path& dismExe, const std::filesystem::path& mountDir,
                                std::wstring_view arguments) {
    // A trailing backslash would escape the closing quote ("C:\m\" → C:\m").
    std::wstring image = mountDir.wstring();
    while (image.size() > 3 && (image.back() == L'\\' || image.back() == L'/')) {
        image.pop_back();
    }
    return std::format(L"\"{}\" /English /Image:\"{}\" {}", dismExe.wstring(), image, arguments);
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

Result<DismExeRun> runDismExe(DismSession& session, std::wstring_view arguments,
                              const std::function<void(double)>& onPercent) {
    const auto exe = dismExePath();
    if (!exe) {
        return std::unexpected(exe.error());
    }
    const std::wstring line = dismExeCommandLine(*exe, session.mountPath(), arguments);
    log::info("dism", line);

    DismExeRun run;
    session.suspend(); // dism.exe opens its own session on the image: ours must not be in the way
    auto exit = runProcess(line, [&](std::string_view chunk) {
        if (onPercent) {
            if (const auto percent = lastDismPercent(chunk)) {
                onPercent(*percent);
            }
        }
        run.output.append(chunk);
        if (run.output.size() > 65536) {
            run.output.erase(0, run.output.size() - 32768);
        }
    });
    auto reopened = session.reload();
    if (!exit) {
        return std::unexpected(exit.error());
    }
    run.exitCode = *exit;
    // What DISM said, without the progress bar redraws.
    std::wstring current;
    for (const char c : run.output) {
        if (c == '\r' || c == '\n') {
            if (!current.empty() && current.find(L"[=") == std::wstring::npos && current.find(L"%") == std::wstring::npos) {
                log::debug("dism", current);
                if (current.starts_with(L"Error") || !run.message.empty()) {
                    run.message += (run.message.empty() ? L"" : L" ") + current;
                }
            }
            current.clear();
        } else if (c != '\0') {
            current.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
    }
    if (!reopened) {
        return std::unexpected(reopened.error());
    }
    return run;
}

} // namespace wl::core
