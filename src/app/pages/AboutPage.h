#pragma once
// P17 Hakkında (docs/pages/17-about.md, screen 20; D-072): the mark, name and version line; the
// Turkish flag with "made with love", what WinLove promises (free, no ads, no data); the links
// (developer, project, issues, license) as link buttons; then the system rows (DISM, work folder,
// fonts, third party). The page's actions — GitHub'da aç, Sorun bildir | Lisanslar, Log klasörünü
// aç — are its ribbon (Shell, 2026-11-10; they were buttons in the hero and the Sistem card).
// Nothing here can be queued or applied.
#include "app/Localization.h"
#include "app/shell/PageView.h"
#include "app/state/AppState.h"
#include "ui/widgets/Button.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace wl::app {

// D-140: what the Güncellemeler card says for the update check's state (AppState::appUpdate and the
// settings), worked out when they change — the card only draws it. `now`: seconds since 1970.
struct UpdateCardView {
    enum class Tone : std::uint8_t { Quiet, Success, Accent, Warning };
    ui::icons::Icon icon = ui::icons::Icon::Refresh;
    Tone tone = Tone::Quiet;
    std::wstring title;
    std::wstring detail;
    std::wstring notes;     // the new release's first lines (available only)
    bool checking = false;  // "Şimdi denetle" waits
    bool available = false; // a newer release
    std::wstring url;       // the release page (core::releasePageUrl)
    // D-145: what the card offers. "Yükle" (`install`; "Yeniden dene" with `retry`), "Değişiklikleri gör"
    // (the releases are known), the download's meter and "İptal" (`installing`), "Yeniden başlat"
    // (`restart`), "İndirme sayfasını aç" (`openDownload`: the update cannot be installed from here).
    bool install = false;
    bool retry = false;
    bool changelog = false;
    bool installing = false;
    float progress = 0;
    std::wstring progressText;
    bool restart = false;
    bool openDownload = false;
    bool operator==(const UpdateCardView&) const = default;
};
[[nodiscard]] UpdateCardView updateCardView(const AppState& state, const Localization& strings, Language language,
                                            std::int64_t now);

class AboutPage : public ui::Widget, public ScrollableBody {
public:
    struct Intents {
        std::function<void(const std::wstring&)> openLink; // a https:// address, in the browser
        std::function<void()> checkUpdates;                 // D-140 "Şimdi denetle" (none: renders, tests)
        // D-145 (none: renders, tests — the buttons are drawn, a click does nothing).
        std::function<void()> install;
        std::function<void()> cancelInstall;
        std::function<void()> restart;
        std::function<void()> changelog;
    };
    AboutPage(AppState& state, const Localization& strings, Language language, Intents intents);
    ~AboutPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    // 2026-11-09: a short window scrolls the page (the Sistem card was cut off).
    [[nodiscard]] float contentHeight() const override;
    [[nodiscard]] const UpdateCardView& updateView() const noexcept { return m_update; } // tests
    [[nodiscard]] ui::Button& checkButton() const noexcept { return *m_checkButton; }   // tests
    [[nodiscard]] ui::Button& releaseButton() const noexcept { return *m_releaseButton; }
    [[nodiscard]] ui::Button& installButton() const noexcept { return *m_installButton; }     // D-145
    [[nodiscard]] ui::Button& changelogButton() const noexcept { return *m_changelogButton; }
    [[nodiscard]] ui::Button& restartButton() const noexcept { return *m_restartButton; }
    [[nodiscard]] ui::Button& cancelButton() const noexcept { return *m_cancelButton; }

    // Where the links go (README and the release use the same).
    static constexpr const wchar_t* kDeveloperUrl = L"https://github.com/shadesofdeath";
    static constexpr const wchar_t* kProjectUrl = L"https://github.com/shadesofdeath/WinLove";
    static constexpr const wchar_t* kIssuesUrl = L"https://github.com/shadesofdeath/WinLove/issues";
    static constexpr const wchar_t* kLicenseUrl = L"https://github.com/shadesofdeath/WinLove/blob/main/LICENSE";

private:
    struct Cards {
        ui::RectF hero, update, love, links, system;
    };
    [[nodiscard]] Cards cards() const; // D-114; D-140: the update card under the hero
    struct Link {
        Str label;
        ui::Button* button;
    };
    void refresh();
    void refreshUpdate(); // D-140
    [[nodiscard]] float updateNotesHeight(float width) const;
    [[nodiscard]] float updateTextX(const ui::RectF& card) const;

    UpdateCardView m_update;
    ui::Button* m_checkButton = nullptr;
    ui::Button* m_releaseButton = nullptr;
    ui::Button* m_installButton = nullptr;   // D-145
    ui::Button* m_changelogButton = nullptr;
    ui::Button* m_restartButton = nullptr;
    ui::Button* m_cancelButton = nullptr;
    mutable float m_notesHeight = 0.0f;
    mutable float m_notesWidth = -1.0f;
    [[nodiscard]] float freeBodyHeight(float width) const;
    // A Sistem row as tall as its value needs at `cardWidth` (long paths wrap, never "…").
    [[nodiscard]] float systemRowHeight(std::size_t row, float cardWidth) const;
    [[nodiscard]] float systemRowsHeight(float cardWidth) const;

    AppState& m_state;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<std::pair<std::wstring, std::pair<std::wstring, bool>>> m_rows; // label → (value, mono)
    std::wstring m_version;
    std::vector<Link> m_links;
    // Wrapped-text heights, kept per width (audit PG-19: paint() and each of its rows measured
    // the wrapped texts again on every frame). refresh() drops the rows' (their values changed).
    mutable std::vector<float> m_rowHeights; // per m_rows entry; < 0: not measured
    mutable float m_rowHeightsWidth = -1.0f; // the card width they were measured for
    mutable float m_freeBodyHeight = 0.0f;
    mutable float m_freeBodyWidth = -1.0f;
    // The hero's pills, measured once (their texts never change; paint measured them per frame).
    struct Pill {
        Str text;
        float width = 0;
    };
    std::vector<Pill> m_pills;
};

} // namespace wl::app
