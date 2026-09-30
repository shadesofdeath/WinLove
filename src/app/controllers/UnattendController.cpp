#include "app/controllers/UnattendController.h"

#include "base/Log.h"
#include "base/Utf8.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>

namespace wl::app {

namespace {

std::string fileBytes(const std::wstring& xml) {
    std::wstring text;
    text.reserve(xml.size() + xml.size() / 20);
    for (const wchar_t c : xml) {
        if (c == L'\n') {
            text.push_back(L'\r');
        }
        text.push_back(c);
    }
    return utf8::fromWide(text);
}

} // namespace

UnattendController::UnattendController(AppState& state) : m_state(state) {}

core::UnattendOptions UnattendController::effective(const AppState& state) {
    core::UnattendOptions options = state.unattend().options;
    if (const auto& source = state.source(); source && !source->install.images.empty()) {
        const auto* image = state.selectedImage();
        const auto architecture = (image ? *image : source->install.images.front()).architecture;
        if (architecture != core::Architecture::Unknown) {
            options.architecture = architecture;
        }
        // The edition Setup will install, when the image leaves no doubt: its generic key goes
        // into the file when the user gave none (Unattend.h).
        const auto& images = source->install.images;
        if (options.imageIndex > 0) {
            if (const auto at = std::ranges::find(images, options.imageIndex, &core::ImageInfo::index); at != images.end()) {
                options.editionId = at->editionId;
            }
        } else if (images.size() == 1) {
            options.editionId = images.front().editionId;
        }
    }
    return options;
}

void UnattendController::edit(const std::function<void(core::UnattendOptions&)>& change) {
    AppState::Unattend unattend = m_state.unattend();
    const bool untouched = unattend.options == core::UnattendOptions{};
    change(unattend.options);
    if (unattend.options == m_state.unattend().options) {
        return;
    }
    if (untouched) {
        // The first answer: a file somebody fills in is meant for the ISO. Unchecking "ISO'ya
        // ekle" afterwards stays unchecked.
        unattend.includeInIso = true;
    }
    m_state.setUnattend(std::move(unattend));
}

void UnattendController::setIncludeInIso(bool include) {
    AppState::Unattend unattend = m_state.unattend();
    if (unattend.includeInIso != include) {
        unattend.includeInIso = include;
        m_state.setUnattend(std::move(unattend));
    }
}

std::wstring UnattendController::xml() const {
    return core::buildUnattendXml(options());
}

std::vector<core::UnattendProblem> UnattendController::problems() const {
    return core::validateUnattend(options());
}

Result<void> UnattendController::save(const std::filesystem::path& file) const {
    const std::string bytes = fileBytes(xml());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        return fail(ErrorCode::IoError, L"could not write the answer file", file.wstring());
    }
    log::info("app", L"answer file saved: " + file.wstring());
    return {};
}

Result<void> UnattendController::import(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::NotFound, L"could not open the answer file", file.wstring());
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = core::parseUnattendXml(bytes);
    if (!parsed) {
        auto error = parsed.error();
        error.context = file.filename().wstring() + (error.context.empty() ? L"" : L" · " + error.context);
        return std::unexpected(std::move(error));
    }
    AppState::Unattend unattend = m_state.unattend();
    unattend.options = std::move(*parsed);
    unattend.includeInIso = true; // an answer file brought in is one to use
    m_state.setUnattend(std::move(unattend));
    log::info("app", L"answer file imported: " + file.wstring());
    return {};
}

std::string UnattendController::isoFile(const AppState& state) {
    if (!state.unattend().includeInIso) {
        return {};
    }
    return fileBytes(core::buildUnattendXml(effective(state)));
}

std::vector<UnattendController::Choice> UnattendController::languages() const {
    std::vector<Choice> result;
    if (const auto& source = m_state.source()) {
        for (const auto& image : source->install.images) {
            for (const auto& language : image.languages) {
                if (std::ranges::find(result, language, &Choice::value) == result.end()) {
                    const auto known = std::ranges::find(locales(), language, &Choice::value);
                    result.push_back({language, known != locales().end() ? known->label : language});
                }
            }
        }
    }
    return result.empty() ? locales() : result;
}

std::vector<UnattendController::Choice> UnattendController::editions() const {
    std::vector<Choice> result;
    if (const auto& source = m_state.source()) {
        for (const auto& image : source->install.images) {
            result.push_back({std::to_wstring(image.index), std::format(L"{} · {}", image.index, image.name)});
        }
    }
    return result;
}

const std::vector<UnattendController::Choice>& UnattendController::locales() {
    static const std::vector<Choice> list{
        {L"tr-TR", L"Türkçe (tr-TR)"},
        {L"en-US", L"English — United States (en-US)"},
        {L"en-GB", L"English — United Kingdom (en-GB)"},
        {L"de-DE", L"Deutsch (de-DE)"},
        {L"fr-FR", L"Français (fr-FR)"},
        {L"es-ES", L"Español (es-ES)"},
        {L"it-IT", L"Italiano (it-IT)"},
        {L"nl-NL", L"Nederlands (nl-NL)"},
        {L"pl-PL", L"Polski (pl-PL)"},
        {L"pt-BR", L"Português — Brasil (pt-BR)"},
        {L"pt-PT", L"Português — Portugal (pt-PT)"},
        {L"ru-RU", L"Russian (ru-RU)"},
        {L"uk-UA", L"Ukrainian (uk-UA)"},
        {L"az-Latn-AZ", L"Azərbaycan (az-Latn-AZ)"},
        {L"ar-SA", L"Arabic (ar-SA)"},
        {L"ja-JP", L"Japanese (ja-JP)"},
        {L"ko-KR", L"Korean (ko-KR)"},
        {L"zh-CN", L"Chinese — Simplified (zh-CN)"},
    };
    return list;
}

const std::vector<UnattendController::Choice>& UnattendController::keyboards() {
    static const std::vector<Choice> list{
        {L"041f:0000041f", L"Türkçe Q"},
        {L"041f:0001041f", L"Türkçe F"},
        {L"0409:00000409", L"US"},
        {L"0409:00020409", L"US International"},
        {L"0809:00000809", L"United Kingdom"},
        {L"0407:00000407", L"German"},
        {L"040c:0000040c", L"French"},
        {L"040a:0000040a", L"Spanish"},
        {L"0410:00000410", L"Italian"},
        {L"0415:00000415", L"Polish (Programmers)"},
        {L"0416:00000416", L"Portuguese (Brazil ABNT)"},
        {L"0419:00000419", L"Russian"},
    };
    return list;
}

const std::vector<UnattendController::Choice>& UnattendController::timeZones() {
    static const std::vector<Choice> list{
        {L"Turkey Standard Time", L"(UTC+03:00) Istanbul"},
        {L"UTC", L"(UTC) Coordinated Universal Time"},
        {L"GMT Standard Time", L"(UTC+00:00) London, Dublin, Lisbon"},
        {L"W. Europe Standard Time", L"(UTC+01:00) Berlin, Amsterdam, Rome, Vienna"},
        {L"Romance Standard Time", L"(UTC+01:00) Paris, Madrid, Brussels"},
        {L"Central European Standard Time", L"(UTC+01:00) Warsaw, Zagreb"},
        {L"GTB Standard Time", L"(UTC+02:00) Athens, Bucharest"},
        {L"FLE Standard Time", L"(UTC+02:00) Kyiv, Helsinki, Sofia"},
        {L"Russian Standard Time", L"(UTC+03:00) Moscow"},
        {L"Arab Standard Time", L"(UTC+03:00) Riyadh, Kuwait"},
        {L"Azerbaijan Standard Time", L"(UTC+04:00) Baku"},
        {L"India Standard Time", L"(UTC+05:30) New Delhi"},
        {L"China Standard Time", L"(UTC+08:00) Beijing, Hong Kong"},
        {L"Tokyo Standard Time", L"(UTC+09:00) Tokyo"},
        {L"Eastern Standard Time", L"(UTC-05:00) Eastern Time (US)"},
        {L"Central Standard Time", L"(UTC-06:00) Central Time (US)"},
        {L"Pacific Standard Time", L"(UTC-08:00) Pacific Time (US)"},
    };
    return list;
}

} // namespace wl::app
