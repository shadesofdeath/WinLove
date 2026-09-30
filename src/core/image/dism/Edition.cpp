#include "core/image/dism/Edition.h"

#include "base/Log.h"
#include "core/image/dism/DismExe.h"

#include <algorithm>
#include <cwctype>
#include <format>

namespace wl::core {

ImageEditions parseEditions(std::string_view currentOutput, std::string_view targetsOutput) {
    ImageEditions editions;
    if (auto current = dismExeValues(currentOutput, "Current Edition"); !current.empty()) {
        editions.current = std::move(current.front());
    }
    editions.targets = dismExeValues(targetsOutput, "Target Edition");
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

ImageText textAfterEditionChange(const ImageInfo& image, std::wstring_view newEditionId) {
    const std::wstring before = editionDisplayName(image.editionId, image.build);
    const std::wstring after = editionDisplayName(newEditionId, image.build);
    // What Setup shows is the display name; images without one have only the name.
    const std::wstring& name = image.displayName.empty() ? image.name : image.displayName;
    const std::wstring& description = image.displayDescription.empty() ? image.description : image.displayDescription;
    ImageText text;
    text.name = name == before || name.empty() ? after : name;
    text.description = description == before || description.empty() ? after : description;
    text.flags = std::wstring(newEditionId);
    return text;
}

Result<ImageEditions> readEditions(DismSession& session) {
    const auto current = runDismExe(session, L"/Get-CurrentEdition");
    if (!current) {
        return std::unexpected(current.error());
    }
    if (current->exitCode != 0) {
        return std::unexpected(dismExeFailure(*current, L"could not read the edition of the image"));
    }
    const auto targets = runDismExe(session, L"/Get-TargetEditions");
    if (!targets) {
        return std::unexpected(targets.error());
    }
    if (targets->exitCode != 0) {
        return std::unexpected(dismExeFailure(*targets, L"could not read the editions the image can be changed to"));
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
        return std::unexpected(dismExeFailure(*run, L"the edition could not be changed"));
    }
    task.report(1.0, L"Set-Edition");
    return {};
}

} // namespace wl::core
