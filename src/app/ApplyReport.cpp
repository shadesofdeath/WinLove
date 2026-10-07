#include "app/ApplyReport.h"

#include "app/Format.h"
#include "app/pages/ApplyPage.h"
#include "base/Utf8.h"

#include <format>

namespace wl::app {

using core::ops::Phase;

namespace {

std::wstring escaped(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::chrono::local_seconds localTime(std::chrono::system_clock::time_point when) {
    return std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_local(when));
}

constexpr const wchar_t* kStyle =
    L"body{font:13px/1.5 'Segoe UI',sans-serif;color:#1d1b19;background:#f6f3ee;margin:32px auto;max-width:960px;padding:0 24px}"
    L"h1{font-size:18px;margin:0 0 4px}h2{font-size:13px;text-transform:uppercase;letter-spacing:.04em;color:#6b655d;"
    L"margin:28px 0 8px;border-bottom:1px solid #ddd6cc;padding-bottom:4px}"
    L".meta{color:#6b655d}table{border-collapse:collapse;width:100%}td,th{padding:4px 8px;text-align:left;"
    L"border-bottom:1px solid #e6e0d6;vertical-align:top}th{font-weight:600;color:#6b655d;font-size:11px}"
    L".n{text-align:right;font-family:Consolas,monospace;white-space:nowrap}.ok{color:#2f7d4f}.skip{color:#b0561f}"
    L".none{color:#8a8379}.stats td{border:0;padding:2px 24px 2px 0}.stats td:first-child{color:#6b655d}"
    L".name{word-break:break-all}";

} // namespace

std::wstring applyPhaseName(const Localization& strings, Phase phase) {
    switch (phase) {
    case Phase::Edition: return strings.get(Str::ApplyOpsEdition);
    case Phase::Remove: return strings.get(Str::ApplyOpsComponents);
    case Phase::Features: return strings.get(Str::ApplyOpsFeatures);
    case Phase::Drivers: return strings.get(Str::ApplyOpsDrivers);
    case Phase::Updates: return strings.get(Str::ApplyOpsUpdates);
    case Phase::Apps: return strings.get(Str::ApplyOpsApps);
    case Phase::DeepRemove: return strings.get(Str::ApplyOpsDeepRemove);
    case Phase::Cleanup: return strings.get(Str::ApplyOpsCleanup);
    case Phase::Settings: return strings.get(Str::ApplyOpsSettings);
    case Phase::Shrink: return strings.get(Str::ApplyOpsShrink);
    }
    return {};
}

std::wstring applySkipReason(const Localization& strings, const Error& error) {
    switch (static_cast<std::uint32_t>(error.hresult)) {
    case 0x80073CFA: return strings.get(Str::ApplyReasonAppxProtected);
    case 0x800F0825: return strings.get(Str::ApplyReasonPermanentPackage);
    case 0x800F0806: return strings.get(Str::ApplyReasonPending);
    default: break;
    }
    if (error.code == ErrorCode::Cancelled) {
        return strings.get(Str::ApplyReasonCancelled);
    }
    // Whatever DISM said, with the code someone can search for.
    return error.hresult != 0 ? std::format(L"{} (0x{:08X})", error.message, static_cast<std::uint32_t>(error.hresult))
                              : error.message;
}

std::wstring applyReportFileName(std::chrono::system_clock::time_point when) {
    return std::format(L"WinLove-rapor-{:%Y%m%d-%H%M}.html", localTime(when));
}

std::string applyReportHtml(const AppState& state, const AppState::ApplyRun& run, const Localization& strings,
                            Language language, std::chrono::system_clock::time_point when) {
    auto s = [&](Str key) { return escaped(strings.get(key)); };
    const auto* result = run.result ? &*run.result : nullptr;
    const std::size_t total = run.plan.steps.size();
    const std::size_t ran = result ? result->report.results.size() : 0;
    const std::size_t failed = result ? result->report.failures() : 0;

    std::wstring html = L"<!doctype html>\n<html lang=\"";
    html += language == Language::Turkish ? L"tr" : L"en";
    html += L"\"><head><meta charset=\"utf-8\"><title>" + s(Str::ApplyReportTitle) + L"</title><style>" + kStyle +
            L"</style></head><body>\n";
    html += L"<h1>" + s(Str::ApplyReportTitle) + L"</h1>\n<div class=\"meta\">" +
            std::format(L"{:%Y-%m-%d %H:%M}", localTime(when)) + L"</div>\n";

    auto stat = [&](const std::wstring& key, const std::wstring& value, const wchar_t* cls = L"") {
        html += L"<tr><td>" + key + L"</td><td class=\"" + cls + L"\">" + escaped(value) + L"</td></tr>\n";
    };
    html += L"<h2>" + s(Str::IsoSummary) + L"</h2>\n<table class=\"stats\">\n";
    stat(s(Str::ApplyReportImage), run.edition);
    if (const auto& source = state.source()) {
        stat(s(Str::IsoSource), source->path.wstring());
    }
    stat(s(Str::ApplyBefore), run.sizeBefore ? formatBytes(run.sizeBefore, language) : std::wstring(L"—"));
    stat(s(Str::ApplyAfter), run.sizeAfter ? formatBytes(run.sizeAfter, language) : std::wstring(L"—"));
    if (run.sizeAfter && run.sizeBefore > run.sizeAfter) {
        const std::uint64_t gain = run.sizeBefore - run.sizeAfter;
        stat(s(Str::ApplyGain),
             std::format(L"{} · %{}", formatBytes(gain, language),
                         static_cast<int>(100.0 * static_cast<double>(gain) / static_cast<double>(run.sizeBefore) + 0.5)),
             L"ok");
    }
    if (result) {
        stat(s(Str::ApplyDuration), formatDuration(static_cast<double>(result->elapsed.count()) / 1000.0, language));
    }
    stat(s(Str::ApplySteps), std::format(L"{} / {}", ran - failed, total), failed || ran < total ? L"skip" : L"ok");
    if (failed) {
        stat(s(Str::ApplySkippedStep), std::to_wstring(failed), L"skip");
    }
    if (run.error) {
        stat(s(Str::ApplyResult), run.error->message + L" — " + run.error->context, L"skip");
    } else if (result && result->commitError) {
        stat(s(Str::ApplyResult), strings.get(Str::ApplyReportNotCommitted) + L": " + result->commitError->message, L"skip");
    } else if (result) {
        stat(s(Str::ApplyResult), strings.get(result->committed ? Str::ApplyReportCommitted : Str::ApplyReportNotCommitted),
             result->committed ? L"ok" : L"skip");
    }
    html += L"</table>\n";

    html += L"<h2>" + s(Str::ApplyOperation) + L"</h2>\n<table>\n<tr><th>#</th><th>" + s(Str::ComponentsCategory) + L"</th><th>" +
            s(Str::ApplyOperation) + L"</th><th>" + s(Str::ApplyResult) + L"</th><th class=\"n\">" + s(Str::ApplyDuration) +
            L"</th></tr>\n";
    for (std::size_t i = 0; i < total; ++i) {
        const auto& step = run.plan.steps[i];
        std::wstring outcome = strings.get(Str::ApplyReportNotRun);
        const wchar_t* cls = L"none";
        if (result && i < result->report.results.size()) {
            const auto& r = result->report.results[i];
            if (r.outcome) {
                outcome = strings.get(Str::ApplyReportOk);
                cls = L"ok";
            } else {
                outcome = strings.get(Str::ApplySkippedStep) + L": " + applySkipReason(strings, r.outcome.error());
                cls = L"skip";
            }
        }
        std::wstring time;
        if (result && i < result->stepTimes.size()) {
            time = std::format(L"{:.1f} s", static_cast<double>(result->stepTimes[i].count()) / 1000.0);
        }
        html += std::format(L"<tr><td class=\"n\">{}</td><td>{}</td><td class=\"name\">{}</td><td class=\"{}\">{}</td><td class=\"n\">{}</td></tr>\n",
                            i + 1, escaped(applyPhaseName(strings, step.phase)),
                            escaped(ApplyPage::displayName(state, step.operation)), cls, escaped(outcome), time);
    }
    html += L"</table>\n</body></html>\n";
    return utf8::fromWide(html);
}

} // namespace wl::app
