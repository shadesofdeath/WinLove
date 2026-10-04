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
#include "core/updates/UupLanguages.h"

#include <functional>
#include <map>
#include <set>
#include <span>
#include <memory>
#include <string>
#include <vector>

namespace wl::app {

// D-061: one row of the Diller page — a language the image has or gets.
struct LanguageRow {
    using Kind = core::LanguagePackFile::Kind;
    std::wstring language;          // "en-US"
    bool inImage = false;           // its language pack is installed
    bool ui = false;                // the image's display language now
    std::vector<Kind> installed;    // its features in the image (Basic, Handwriting, …)
    std::vector<Kind> queued;       // its files in the queue (LanguagePack, Basic, …; no Satellite)
    int componentsInImage = 0;      // languages of components (satellites) installed
    int componentsQueued = 0;
    std::uint64_t queuedBytes = 0;
    [[nodiscard]] bool has(Kind k) const; // installed or queued
};

// The optional parts picked in "Dil ekle…" (the pack, Basic and the fonts a script needs always go).
struct LanguageParts {
    bool handwriting = true;
    bool ocr = true;
    bool textToSpeech = true;
    bool speech = true;
    bool components = true; // the languages of the components the image has
};

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
    // `chosen` plus what it needs from `available` that the image lacks: the language pack of a
    // feature's language, the features a feature depends on, the fonts of the pack's script.
    [[nodiscard]] std::vector<core::LanguagePackFile> withDependencies(std::vector<core::LanguagePackFile> chosen,
                                                                       const std::vector<core::LanguagePackFile>& available) const;
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
    // Language names follow the app's language: English names in English, Windows' own otherwise.
    static void setNameLanguage(Language language);

    [[nodiscard]] int changedCount() const; // nav badge: language packages + SetIntl

    // ---- D-061: the page's language list, downloads ----
    [[nodiscard]] std::vector<LanguageRow> rows() const;   // image languages first, then queued ones
    void unqueueLanguage(std::wstring_view language);      // every queued file of it
    [[nodiscard]] std::span<const std::wstring> imagePackages() const; // installed identities (empty until read)
    [[nodiscard]] bool imageHasLanguage(std::wstring_view language) const;
    // The files of `language` to download for `parts`: dependencies kept (Speech takes
    // TextToSpeech), components only those the image has, nothing the image already has.
    [[nodiscard]] std::vector<core::UupLanguageFile> pick(const core::UupLanguage& language, const LanguageParts& parts) const;
    // Downloaded language files into the queue; `uiLanguage` (optional) becomes the display language.
    void queueFiles(const std::vector<std::filesystem::path>& files, const std::wstring& uiLanguage = {});
    // Microsoft: a language added after the cumulative update keeps the base version of its files
    // until that update is installed again. True when languages are queued, the image has an
    // update (revision > 1) and no cumulative update is queued after them.
    [[nodiscard]] bool cumulativeUpdateAdvised() const;

private:
    // What the image's packages say, read once per package list (they are ~800 identities).
    struct PackageIndex {
        const std::wstring* data = nullptr; // the list it was built from
        std::size_t size = 0;
        std::set<std::wstring> neutral;      // "microsoft-windows-mediaplayer-package~wow64": components
        std::set<std::wstring> localized;    // "…-package~wow64~en-us": a component's language installed
        std::set<std::wstring> fonts;        // "jpan"
        std::map<std::wstring, std::vector<core::LanguagePackFile::Kind>> features; // "en-us" → Basic, …
        std::map<std::wstring, int> components; // "en-us" → component languages installed
    };
    [[nodiscard]] const PackageIndex& index() const;

    AppState& m_state;
    std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    mutable PackageIndex m_index;
};

} // namespace wl::app
