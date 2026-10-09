#include "core/image/dism/ImageHealth.h"

#include "core/image/dism/DismExe.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>

namespace wl::core {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// The one DISM sentence that carries the verdict ("The component store is repairable.", "No
// component store corruption detected.", "The restore operation completed successfully."), for the
// UI and the log. Empty when none is present. ASCII in, widened out.
std::wstring healthSentence(std::string_view output) {
    static constexpr std::string_view marks[] = {"component store corruption", "component store is",
                                                 "restore operation completed", "store cannot be repaired"};
    std::string_view best;
    const std::string low = lower(output);
    for (const auto mark : marks) {
        const auto at = low.find(mark);
        if (at == std::string::npos) {
            continue;
        }
        auto begin = output.rfind('\n', at);
        begin = (begin == std::string_view::npos) ? 0 : begin + 1;
        auto end = output.find('\n', at);
        end = (end == std::string_view::npos) ? output.size() : end;
        std::string_view line = output.substr(begin, end - begin);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\r')) {
            line.remove_prefix(1);
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            best = line; // the last verdict sentence in reading order wins
        }
    }
    std::wstring wide;
    wide.reserve(best.size());
    for (const char c : best) {
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return wide;
}

} // namespace

ImageHealthState imageHealthFromOutput(std::string_view output) {
    const std::string text = lower(output);
    // Order matters: "repaired" / "repairable" both contain "repair", so test the exact phrases.
    if (has(text, "no component store corruption")) {
        return ImageHealthState::Healthy;
    }
    if (has(text, "cannot be repaired") || has(text, "non-repairable")) {
        return ImageHealthState::NonRepairable;
    }
    if (has(text, "store is repairable") || has(text, "corruption was repaired") ||
        has(text, "restore operation completed")) {
        return ImageHealthState::Repairable;
    }
    return ImageHealthState::Unknown;
}

Result<ImageHealthReport> checkImageHealth(DismSession& session, bool scan, const TaskContext& task) {
    const wchar_t* label = scan ? L"ScanHealth" : L"CheckHealth";
    if (auto go = task.cancel.check(scan ? L"image health scan" : L"image health check"); !go) {
        return std::unexpected(go.error());
    }
    const std::wstring arguments = scan ? L"/Cleanup-Image /ScanHealth" : L"/Cleanup-Image /CheckHealth";
    const auto run = runDismExe(session, arguments, [&](double percent) { task.report(percent, label); });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0) {
        return std::unexpected(dismExeFailure(
            *run, std::format(L"image health {} failed", scan ? L"scan" : L"check")));
    }
    task.report(1.0, label);
    ImageHealthReport report;
    report.scanned = scan;
    report.state = imageHealthFromOutput(run->output);
    report.detail = healthSentence(run->output); // DISM's own verdict line, for the UI/log
    return report;
}

Result<ImageHealthReport> restoreImageHealth(DismSession& session, std::wstring_view source, bool limitAccess,
                                             const TaskContext& task) {
    if (auto go = task.cancel.check(L"image health restore"); !go) {
        return std::unexpected(go.error());
    }
    std::wstring arguments = L"/Cleanup-Image /RestoreHealth";
    if (!source.empty()) {
        arguments += L" /Source:";
        arguments += source;
    }
    if (limitAccess) {
        arguments += L" /LimitAccess";
    }
    const auto run = runDismExe(session, arguments, [&](double percent) { task.report(percent, L"RestoreHealth"); });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0) {
        // 0x800F081F: the source files could not be found — the usual reason an offline repair needs a source.
        std::wstring hint = L"image health restore failed";
        if (run->exitCode == 0x800F081F) {
            hint = L"image health restore could not find repair files — give a matching install.wim/ESD as the source";
        }
        return std::unexpected(dismExeFailure(*run, hint));
    }
    task.report(1.0, L"RestoreHealth");
    ImageHealthReport report;
    report.scanned = true;
    const bool wasHealthy = imageHealthFromOutput(run->output) == ImageHealthState::Healthy;
    report.repaired = !wasHealthy; // a non-"no corruption" success means it mended something
    report.state = ImageHealthState::Healthy;
    report.detail = healthSentence(run->output);
    return report;
}

} // namespace wl::core
