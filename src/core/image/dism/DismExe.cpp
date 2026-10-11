#include "core/image/dism/DismExe.h"

#include "base/Log.h"
#include "core/image/dism/Dism.h"
#include "core/image/dism/HostDism.h"
#include "core/system/Process.h"
#include "core/uup/PsfPackage.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <vector>

namespace wl::core {

Result<std::filesystem::path> dismExePath() {
    // System32's, or the ADK's when this PC's DISM is missing a part (HostDism.h).
    return dismLocation().dismExe;
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

// Every "<label> : <value>" line of a piece of dism.exe output.
std::vector<std::wstring> dismExeValues(std::string_view output, std::string_view label) {
    std::vector<std::wstring> values;
    for (std::size_t at = 0; at < output.size();) {
        const std::size_t end = std::min(output.find_first_of("\r\n", at), output.size());
        std::string_view line = output.substr(at, end - at);
        at = end + 1;
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || !line.substr(first).starts_with(label)) {
            continue;
        }
        line.remove_prefix(first + label.size());
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || line.substr(0, colon).find_first_not_of(" \t") != std::string_view::npos) {
            continue;
        }
        line.remove_prefix(colon + 1);
        const auto start = line.find_first_not_of(" \t");
        const auto stop = line.find_last_not_of(" \t");
        if (start != std::string_view::npos) {
            values.emplace_back(line.begin() + static_cast<std::ptrdiff_t>(start),
                                line.begin() + static_cast<std::ptrdiff_t>(stop) + 1);
        }
    }
    return values;
}

Error dismExeFailure(const DismExeRun& run, std::wstring message) {
    return Error{ErrorCode::DismFailure, std::move(message),
                 run.message.empty() ? std::format(L"dism.exe exit code 0x{:08X}", run.exitCode) : run.message,
                 static_cast<std::int32_t>(run.exitCode)};
}

namespace {

// Windows Update's agent refused the .msu offline without touching the image.
bool agentRefusal(std::int32_t hresult) noexcept {
    const auto hr = static_cast<std::uint32_t>(hresult);
    return hr == 0x800F8012u || hr == 0x80070057u;
}

TaskContext share(const TaskContext& task, double from, double to) {
    return TaskContext{task.cancel, [&task, from, to](double f, std::wstring_view stage) {
                           task.report(from + (to - from) * std::clamp(f, 0.0, 1.0), stage);
                       }};
}

Result<void> addPlainOrDismExe(DismSession& session, const std::filesystem::path& package, const TaskContext& task) {
    auto added = session.addPackage(package, task);
    constexpr std::int32_t kSessionNotRegistered = static_cast<std::int32_t>(0x800401E3);
    const std::wstring name = package.wstring();
    const bool msu = name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".msu") == 0;
    if (added || !msu || added.error().hresult != kSessionNotRegistered) {
        return added;
    }
    log::warn("dism", L"the DISM API could not take the .msu (0x800401E3); dism.exe installs it: " + name);
    auto run = runDismExe(session, L"/Add-Package /PackagePath:\"" + name + L"\"",
                          [&](double fraction) { task.report(fraction, L"dism.exe"); });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0 && run->exitCode != 3010) { // 3010: done, restart pending (an offline image has none to do)
        return std::unexpected(dismExeFailure(*run, L"add package " + name));
    }
    return {};
}

} // namespace

Result<void> addPackageOrDismExe(DismSession& session, const std::filesystem::path& package, const TaskContext& task) {
    auto added = addPlainOrDismExe(session, package, task);
    if (added || !agentRefusal(added.error().hresult) || task.cancel.cancelled() || uup::msuNames(package).empty()) {
        return added;
    }
    log::warn("dism", std::format(L"Windows Update's agent refused {} offline ({}): it goes in expanded",
                                  package.filename().wstring(), describe(added.error())));
    const auto work = std::filesystem::temp_directory_path() / L"WinLove" / L"msu";
    std::error_code ec;
    std::filesystem::remove_all(work, ec);
    auto expanded = addMsuExpanded(session, package, uup::checkpointsBeside(package), work, /*ssu=*/true, task);
    std::filesystem::remove_all(work, ec);
    return expanded;
}

Result<void> addMsuExpanded(DismSession& session, const std::filesystem::path& msu,
                            const std::vector<std::filesystem::path>& checkpoints, const std::filesystem::path& work, bool ssu,
                            const TaskContext& task) {
    std::vector<std::filesystem::path> msus = checkpoints;
    msus.push_back(msu);
    const auto dlls = session.mountPath() / L"Windows" / L"System32"; // when this PC has no UpdateCompression.dll
    std::vector<uup::ExpandedMsu> expanded;
    for (std::size_t k = 0; k < msus.size(); ++k) {
        const double n = static_cast<double>(msus.size());
        auto one = uup::expandMsu(msus[k], work, share(task, 0.4 * static_cast<double>(k) / n, 0.4 * static_cast<double>(k + 1) / n),
                                  dlls);
        if (!one) {
            return std::unexpected(one.error());
        }
        expanded.push_back(std::move(*one));
    }
    constexpr auto kNotApplicable = static_cast<std::int32_t>(0x800F081E);
    if (ssu && !expanded.back().ssu.empty()) {
        if (auto r = session.addPackage(expanded.back().ssu, share(task, 0.4, 0.45)); !r && r.error().hresult != kNotApplicable) {
            return r;
        }
        // A session keeps the servicing stack it was opened with.
        if (auto r = session.reload(); !r) {
            return r;
        }
    }
    for (std::size_t k = 0; k < expanded.size(); ++k) {
        if (session.reloadRequired()) {
            if (auto r = session.reload(); !r) {
                return r;
            }
        }
        const double from = 0.45 + 0.55 * static_cast<double>(k) / static_cast<double>(expanded.size());
        auto r = session.addPackage(expanded[k].package, share(task, from, from + 0.55 / static_cast<double>(expanded.size())));
        if (!r && k + 1 < expanded.size() && r.error().hresult == kNotApplicable) {
            log::info("dism", L"checkpoint not needed (the image is past it): " + msus[k].filename().wstring());
            continue;
        }
        if (!r) {
            return r;
        }
    }
    return {};
}

} // namespace wl::core
