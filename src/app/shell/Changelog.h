#pragma once
// D-145: "Değişiklikleri gör" — the release notes of every WinLove newer than the running one, newest
// first, then (under "Önceki sürümler") the last few older ones: version, date, tags, the body as
// Markdown (ui::MarkdownView). All of it comes from the one answer the update check already has
// (core::ReleaseCheck::releases): no request is made for the notes. ChangelogView is the root widget of
// its own top-level window (App owns that window); --render draws it offscreen (--demo-changelog).
#include "app/Localization.h"
#include "app/shell/TitleBar.h"
#include "core/net/ReleaseCheck.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/MarkdownView.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wl::app {

// The window's sections for `check` (pure: the unit tests' way in). `now`: seconds since 1970, for
// "2 gün önce". `older`: how many releases at or below the running version follow the newer ones.
[[nodiscard]] std::vector<ui::MarkdownView::Section> changelogSections(const core::ReleaseCheck& check, const Localization& strings,
                                                                       Language language, std::int64_t now, std::size_t older = 3);
// "Bu bilgisayarda 1.3.0 Beta · 2 yeni sürüm".
[[nodiscard]] std::wstring changelogSubtitle(const core::ReleaseCheck& check, const Localization& strings);

class ChangelogView : public ui::Widget {
public:
    struct Services {
        std::function<void()> minimize;
        std::function<void()> toggleMaximize;
        std::function<void()> close;
        std::function<void(const std::wstring& url)> openLink; // https only (the App checks)
        std::function<void()> install;                         // "Yükle" (a newer release); empty: no button
        std::function<void()> openRelease;                     // the newest release's page
    };
    ChangelogView(const Localization& strings, Language language, Services services);

    void setCheck(const core::ReleaseCheck& check, std::int64_t now);
    void setInstallable(bool installable); // "Yükle" shown (a newer release, nothing installing)
    void setMaximized(bool maximized);
    void setWindowActive(bool active);
    [[nodiscard]] ui::MarkdownView& notes() const noexcept { return *m_notes; }

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onKeyDown(const ui::KeyEvent& key) override; // Esc closes
    [[nodiscard]] ui::HitZone windowZone() const override;

private:
    const Localization& m_strings;
    Language m_language;
    Services m_services;
    std::wstring m_title;
    std::wstring m_subtitle;
    std::wstring m_hint;
    bool m_windowActive = true;
    ui::MarkdownView* m_notes = nullptr;
    ui::Button* m_install = nullptr;
    ui::Button* m_page = nullptr;
    CaptionButton* m_minimize = nullptr;
    CaptionButton* m_maximize = nullptr;
    CaptionButton* m_close = nullptr;
};

} // namespace wl::app
