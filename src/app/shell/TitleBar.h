#pragma once
// Title bar as the reference tool draws it (D-109): size::titleBar on bg.titleBar — the ≡ app menu, the
// workspace tabs (Görüntü · Ön Ayar · Araçlar: text, the chosen one with a 2px accent line under
// it), the window title centered, then the theme button and the caption buttons. The command
// palette has no box any more (Ctrl K still opens it). The bar itself reports HitZone::Caption so
// Windows drags the window; caption buttons report their HT* zones (Snap Layouts on maximize).
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
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
    explicit MenuButton(std::wstring label);
    std::function<void()> onInvoke;
    void setActive(bool active);
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] ui::SizeF measure(ui::SizeF available) override;
    void paint(ui::Canvas& canvas) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::menu; }

private:
    std::wstring m_label;
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
    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] ui::HitZone windowZone() const override { return ui::HitZone::Caption; }

private:
    std::wstring m_appName;
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
