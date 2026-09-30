#include "core/image/dism/Edition.h"

#include "base/Log.h"
#include "core/image/dism/DismExe.h"

#include <algorithm>
#include <cwctype>
#include <format>

namespace wl::core {

namespace {

// Every "<label> : <value>" line of a piece of dism.exe output.
std::vector<std::wstring> valuesOf(std::string_view output, std::string_view label) {
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

Error dismFailure(const DismExeRun& run, std::wstring message) {
    return Error{ErrorCode::DismFailure, std::move(message),
                 run.message.empty() ? std::format(L"dism.exe exit code 0x{:08X}", run.exitCode) : run.message,
                 static_cast<std::int32_t>(run.exitCode)};
}

} // namespace

ImageEditions parseEditions(std::string_view currentOutput, std::string_view targetsOutput) {
    ImageEditions editions;
    if (auto current = valuesOf(currentOutput, "Current Edition"); !current.empty()) {
        editions.current = std::move(current.front());
    }
    editions.targets = valuesOf(targetsOutput, "Target Edition");
    return editions;
}

bool isEditionId(std::wstring_view text) noexcept {
    return !text.empty() && text.size() <= 64 &&
           std::ranges::all_of(text, [](wchar_t c) { return c < 128 && std::iswalnum(static_cast<wint_t>(c)); });
}

std::wstring editionDisplayName(std::wstring_view editionId, int build) {
    struct Known {
        std::wstring_view id;
        std::wstring_view name;
    };
    static constexpr Known kEditions[] = {
        {L"Core", L"Home"},
        {L"CoreN", L"Home N"},
        {L"CoreSingleLanguage", L"Home Single Language"},
        {L"CoreCountrySpecific", L"Home China"},
        {L"Professional", L"Pro"},
        {L"ProfessionalN", L"Pro N"},
        {L"ProfessionalSingleLanguage", L"Pro Single Language"},
        {L"ProfessionalCountrySpecific", L"Pro China"},
        {L"ProfessionalEducation", L"Pro Education"},
        {L"ProfessionalEducationN", L"Pro Education N"},
        {L"ProfessionalWorkstation", L"Pro for Workstations"},
        {L"ProfessionalWorkstationN", L"Pro N for Workstations"},
        {L"Education", L"Education"},
        {L"EducationN", L"Education N"},
        {L"Enterprise", L"Enterprise"},
        {L"EnterpriseN", L"Enterprise N"},
        {L"EnterpriseS", L"Enterprise LTSC"},
        {L"EnterpriseSN", L"Enterprise N LTSC"},
        {L"IoTEnterprise", L"IoT Enterprise"},
        {L"IoTEnterpriseS", L"IoT Enterprise LTSC"},
        {L"ServerRdsh", L"Enterprise multi-session"},
        {L"CloudEdition", L"SE"},
        {L"CloudEditionN", L"SE N"},
    };
    const wchar_t* windows = build >= 22000 ? L"Windows 11" : L"Windows 10";
    for (const auto& known : kEditions) {
        if (known.id == editionId) {
            return std::format(L"{} {}", windows, known.name);
        }
    }
    return std::format(L"{} {}", windows, editionId);
}

Result<ImageEditions> readEditions(DismSession& session) {
    const auto current = runDismExe(session, L"/Get-CurrentEdition");
    if (!current) {
        return std::unexpected(current.error());
    }
    if (current->exitCode != 0) {
        return std::unexpected(dismFailure(*current, L"could not read the edition of the image"));
    }
    const auto targets = runDismExe(session, L"/Get-TargetEditions");
    if (!targets) {
        return std::unexpected(targets.error());
    }
    if (targets->exitCode != 0) {
        return std::unexpected(dismFailure(*targets, L"could not read the editions the image can be changed to"));
    }
    ImageEditions editions = parseEditions(current->output, targets->output);
    if (editions.current.empty()) {
        return fail(ErrorCode::ParseError, L"dism.exe did not name the edition of the image");
    }
    log::info("dism", std::format(L"edition {}: {} target edition(s)", editions.current, editions.targets.size()));
    return editions;
}

Result<void> setEdition(DismSession& session, std::wstring_view editionId, const TaskContext& task) {
    if (!isEditionId(editionId)) {
        return fail(ErrorCode::InvalidArgument, L"not an edition id", std::wstring(editionId));
    }
    if (auto go = task.cancel.check(L"edition change"); !go) {
        return go;
    }
    const auto run = runDismExe(session, std::format(L"/Set-Edition:{}", editionId),
                                [&](double percent) { task.report(percent, L"Set-Edition"); });
    if (!run) {
        return std::unexpected(run.error());
    }
    if (run->exitCode != 0) {
        return std::unexpected(dismFailure(*run, L"the edition could not be changed"));
    }
    task.report(1.0, L"Set-Edition");
    return {};
}

} // namespace wl::core
