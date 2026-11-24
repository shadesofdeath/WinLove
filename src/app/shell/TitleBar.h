#pragma once
// Title bar as the reference tool draws it (D-109): size::titleBar on bg.titleBar — the ≡ app menu, the
// workspace tabs (Görüntü · Ön Ayar · Araçlar: text, the chosen one with a 2px accent line under
// it), the window title centered, then the theme button and the caption buttons. The command
// palette has no box any more (Ctrl K still opens it). The bar itself reports HitZone::Caption so
// Windows drags the window; caption buttons report their HT* zones (Snap Layouts on maximize).
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

class PaletteTrigger : public ui::Widget {
public:
    PaletteTrigger(std::wstring hint, std::vector<std::wstring> keys);
    std::function<void()> onInvoke;
    void paint(ui::Canvas& canvas) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::r2; }

private:
    std::wstring m_hint;
    std::vector<std::wstring> m_keys;
};

// A workspace of the title bar: 30px, radius 7; the chosen one on bg.raised in BodySemi.
class MenuButton : public ui::Widget {
public:
    // 2026-11-11 (the owner): an icon before the label, as the navigation's items have.
    explicit MenuButton(std::wstring label, std::optional<ui::icons::Icon> icon = std::nullopt);
    std::function<void()> onInvoke;
    void setActive(bool active);
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] ui::SizeF measure(ui::SizeF available) override;
    void paint(ui::Canvas& canvas) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::menu; }
    // The ring around the label and its accent line, inside the bar (it was cut at the window's top).
    [[nodiscard]] ui::RectF focusRect() const override;

private:
    std::wstring m_label;
    std::optional<ui::icons::Icon> m_icon;
    float m_textWidth = 0; // measure()'s, for the accent line under the chosen one
    bool m_active = false;
};

class CaptionButton : public ui::Widget {
public:
    enum class Kind : std::uint8_t { Minimize, Maximize, Close, Theme, Menu };
    explicit CaptionButton(Kind kind);
    std::function<void()> onInvoke;
    void setMaximized(bool maximized);
    void setWindowActive(bool active);
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] ui::HitZone windowZone() const override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    bool tick(double now) override;
    [[nodiscard]] float focusRadius() const override { return 0; }
    // The ring on the button's own edges (outside them it was cut by the window's top and right).
    [[nodiscard]] ui::RectF focusRect() const override;

private:
    Kind m_kind;
    bool m_maximized = false;
    bool m_windowActive = true;
    ui::Tween m_hover;
};

class TitleBar : public ui::Widget {
public:
    struct Labels {
        std::wstring appName;
        std::wstring paletteHint;
        std::wstring ctrlKey;
        std::wstring minimize;
        std::wstring close;
        std::wstring maximize; // accessible name only: Windows 11 puts Snap Layouts on hover there
        std::wstring restore;
        std::wstring theme;    // the sun / moon button
        std::wstring appMenu;  // the ≡ button
        std::vector<std::wstring> menus; // Görüntü · Ön Ayar · Araçlar
        std::vector<ui::icons::Icon> menuIcons; // one per menu (none: text only)
    };
    explicit TitleBar(const Labels& labels);
    PaletteTrigger& palette() { return *m_palette; }
    CaptionButton& themeButton() { return *m_theme; }
    CaptionButton& menuButton() { return *m_appMenu; }
    CaptionButton& minimizeButton() { return *m_minimize; }
    CaptionButton& maximizeButton() { return *m_maximize; }
    CaptionButton& closeButton() { return *m_close; }
    std::function<void(int menu)> onMenu;
    void setActiveMenu(int menu);
    void setWindowActive(bool active);
    void setMaximized(bool maximized);
    // D-147: the centred title is always the app's name, drawn as the wordmark (TypeStyle::Wordmark,
    // its second word in the accent's text tone); the open image is named in the status bar and on
    // the pages, not here.
    [[nodiscard]] const std::wstring& title() const noexcept { return m_appName; }
    // Where the title is drawn: centred in the bar, moved clear of the menus on the left, cut with
    // "…" before the buttons on the right; empty when not even a few letters fit.
    [[nodiscard]] ui::RectF titleRect() const noexcept { return m_titleRect; }
    [[nodiscard]] static ui::RectF titleRectFor(ui::RectF bar, float textWidth, float leftLimit, float rightLimit);
    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] ui::HitZone windowZone() const override { return ui::HitZone::Caption; }

private:
    std::wstring m_appName;
    std::size_t m_accentFrom = 0; // where the wordmark's accented part starts ("Win|Love"); 0 = none
    float m_leadWidth = 0;        // the part before it, measured in layout()
    float m_titleWidth = 0;       // the whole name
    ui::RectF m_titleRect{};
    std::wstring m_maximizeName;
    std::wstring m_restoreName;
    bool m_windowActive = true;
    float m_menusLeft = 0;
    PaletteTrigger* m_palette = nullptr;
    std::vector<MenuButton*> m_menus;
    CaptionButton* m_theme = nullptr;
    CaptionButton* m_appMenu = nullptr;
    CaptionButton* m_minimize = nullptr;
    CaptionButton* m_maximize = nullptr;
    CaptionButton* m_close = nullptr;
};

} // namespace wl::app
