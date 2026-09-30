#pragma once
// D-053 logic of the Diller page.
// - What the image has: dism.exe /Get-Intl on the engine thread (a few seconds), once per mount,
//   when the page is first shown (AppState::imageIntl).
// - Language packs and features: a folder of Microsoft's language media is scanned; the files of
//   the image's architecture are offered as a check list and queued as AddPackage "language".
// - International settings: one SetIntl operation (JSON; empty fields = unchanged). The UI
//   language offered is one the image has or gets from a queued language pack.
// - The choices of locales, keyboards and time zones come from this PC's Windows (the same lists
//   the image has: they are part of every Windows).
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "core/image/LanguagePacks.h"
#include "core/image/dism/Intl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wl::app {

struct IntlChoice {
    std::wstring value;   // what DISM takes: "tr-TR", "041f:0000041f", "Turkey Standard Time"
    std::wstring display; // "Türkçe (Türkiye)", "Türkçe Q", "(UTC+03:00) İstanbul"
};

class LanguageController {
public:
    LanguageController(AppState& state, std::function<void(std::function<void()>)> postToUi);
    ~LanguageController();

    void load(bool force = false); // the image's international settings

    // ---- language packs ----
    [[nodiscard]] std::wstring imageArchitecture() const;
    // The files of a scanned folder that fit the image, with what is queued of them already.
    [[nodiscard]] std::vector<core::LanguagePackFile> fitting(const std::vector<core::LanguagePackFile>& files) const;
    [[nodiscard]] bool queued(const core::LanguagePackFile& file) const;
    void queuePacks(const std::vector<core::LanguagePackFile>& files);
    [[nodiscard]] std::vector<core::LanguagePackFile> queuedPacks() const;
    void unqueuePack(const std::filesystem::path& file);
    [[nodiscard]] static core::ops::Operation operationFor(const core::LanguagePackFile& file);

    // ---- international settings ----
    [[nodiscard]] core::IntlSettings settings() const; // the queued SetIntl (empty = nothing changes)
    void setSettings(const core::IntlSettings& settings);
    // UI languages the image will have: installed + queued language packs.
    [[nodiscard]] std::vector<std::wstring> uiLanguages() const;

    [[nodiscard]] static const std::vector<IntlChoice>& locales();   // this PC's (sorted by name)
    [[nodiscard]] static const std::vector<IntlChoice>& keyboards(); // this PC's keyboard layouts
    [[nodiscard]] static const std::vector<IntlChoice>& timeZones(); // sorted by offset
    [[nodiscard]] static std::wstring localeName(std::wstring_view tag); // "Türkçe (Türkiye)" or the tag

    [[nodiscard]] int changedCount() const; // nav badge: language packages + SetIntl

private:
    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
