#include "app/controllers/UnattendController.h"

#include "app/controllers/LanguageController.h"
#include "app/generated/StringKeys.g.h"
#include "base/File.h"
#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/generated/Scripts.g.h"

#include <json.hpp>

#include <windows.h>

#include <shellapi.h>

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

std::vector<std::pair<std::string, std::wstring>> UnattendController::welcomeTexts(const Localization& strings) {
    std::vector<std::pair<std::string, std::wstring>> out;
    constexpr std::string_view kPrefix = "welcome.";
    for (std::size_t i = 0; i < kStrCount; ++i) {
        const std::string_view key = kStrKeys[i];
        if (key.starts_with(kPrefix)) {
            out.emplace_back(std::string(key.substr(kPrefix.size())), strings.get(static_cast<Str>(i)));
        }
    }
    return out;
}

core::WelcomePlan UnattendController::welcomePlan() const {
    return core::welcomePlanFromOperations(m_state.changes().operations()).value_or(core::WelcomePlan{});
}

bool UnattendController::welcomeQueued() const {
    return core::welcomePlanFromOperations(m_state.changes().operations()).has_value();
}

std::vector<std::pair<std::string, std::wstring>> UnattendController::welcomeTextsNow() const {
    // The language whoever installs reads: the answer file's UI language, else the app's.
    const std::wstring ui = text::lower(m_state.unattend().options.uiLanguage);
    const Localization* strings = nullptr;
    if (stringsOf) {
        strings = ui.empty() ? stringsOf(m_state.settings().language)
                             : stringsOf(ui.starts_with(L"tr") ? Language::Turkish : Language::English);
    }
    return strings ? welcomeTexts(*strings) : std::vector<std::pair<std::string, std::wstring>>{};
}

void UnattendController::setWelcomePlan(core::WelcomePlan plan) {
    plan.texts = welcomeTextsNow();
    // The answer file's computer name and time zone are the welcome's first answers (D-085: the file
    // leaves them out while the welcome is on).
    const auto& options = m_state.unattend().options;
    plan.timeZone = options.timeZone;
    plan.computerName = options.randomComputerName ? std::wstring() : options.computerName;
    m_state.unqueueMany(core::welcomeSlots());
    m_state.queueMany(core::welcomeOperations(plan));
}

Result<void> UnattendController::previewWelcome() const {
    core::WelcomePlan plan = welcomePlan();
    plan.texts = welcomeTextsNow();
    auto json = nlohmann::json::parse(core::welcomeJson(plan));
    json["preview"] = true; // a window, nothing done (oobe.ps1)
    const auto folder = std::filesystem::temp_directory_path() / L"WinLove" / L"welcome-preview";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (auto r = writeFileAtomic(folder / L"oobe.ps1", core::scripts::kOobe); !r) {
        return r;
    }
    if (auto r = writeFileAtomic(folder / L"oobe.json", json.dump(1)); !r) {
        return r;
    }
    const std::wstring arguments = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + (folder / L"oobe.ps1").wstring() + L"\"";
    const auto started = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"powershell.exe", arguments.c_str(),
                                                                 folder.c_str(), SW_HIDE));
    if (started <= 32) {
        return fail(ErrorCode::IoError, L"the preview could not start", L"powershell.exe", static_cast<std::int32_t>(started));
    }
    return {};
}

void UnattendController::setWelcome(bool on) {
    edit([on](core::UnattendOptions& o) { o.welcome = on; });
    if (on) {
        setWelcomePlan(welcomePlan());
    } else {
        m_state.unqueueMany(core::welcomeSlots());
    }
}

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
    // The queued welcome follows the answers it takes: its language and its first time zone.
    const auto& before = m_state.unattend().options;
    const bool refresh = welcomeQueued() && (unattend.options.uiLanguage != before.uiLanguage || unattend.options.timeZone != before.timeZone ||
                                             unattend.options.computerName != before.computerName ||
                                             unattend.options.randomComputerName != before.randomComputerName);
    m_state.setUnattend(std::move(unattend));
    if (refresh) {
        setWelcomePlan(welcomePlan());
    }
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
