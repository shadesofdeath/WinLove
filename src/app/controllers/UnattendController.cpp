#include "app/controllers/UnattendController.h"

#include "app/controllers/LanguageController.h"
#include "base/File.h"
#include "base/Log.h"
#include "base/Utf8.h"

#include <algorithm>
#include <format>

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
    if (auto written = writeFileAtomic(file, fileBytes(xml())); !written) {
        return written;
    }
    log::info("app", L"answer file saved: " + file.wstring());
    return {};
}

Result<void> UnattendController::import(const std::filesystem::path& file) {
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto parsed = core::parseUnattendXml(*bytes);
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

// One source for the intl lists (Languages page too): this PC's own, named by Windows in its
// display language — no hand-written names mixing languages.
namespace {
std::vector<UnattendController::Choice> choicesOf(const std::vector<IntlChoice>& list) {
    std::vector<UnattendController::Choice> out;
    out.reserve(list.size());
    for (const auto& c : list) {
        out.push_back({c.value, c.display});
    }
    return out;
}
} // namespace

const std::vector<UnattendController::Choice>& UnattendController::locales() {
    static const std::vector<Choice> list = choicesOf(LanguageController::locales());
    return list;
}

const std::vector<UnattendController::Choice>& UnattendController::keyboards() {
    static const std::vector<Choice> list = choicesOf(LanguageController::keyboards());
    return list;
}

const std::vector<UnattendController::Choice>& UnattendController::timeZones() {
    static const std::vector<Choice> list = choicesOf(LanguageController::timeZones());
    return list;
}

} // namespace wl::app
