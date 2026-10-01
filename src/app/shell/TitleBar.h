#pragma once
// Title bar per 03_components/statusbar-titlebar.md (32px): brand + breadcrumb, command palette
// trigger (centered 280×20), caption buttons 46×32. The bar itself reports HitZone::Caption so
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

private:
    std::wstring m_hint;
    std::vector<std::wstring> m_keys;
};

class CaptionButton : public ui::Widget {
public:
    enum class Kind : std::uint8_t { Minimize, Maximize, Close };
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
    };
    explicit TitleBar(const Labels& labels);

    PaletteTrigger& palette() { return *m_palette; }
    CaptionButton& minimizeButton() { return *m_minimize; }
    CaptionButton& maximizeButton() { return *m_maximize; }
    CaptionButton& closeButton() { return *m_close; }

    // statusbar-titlebar.md "Breadcrumb": image › edition › state; last item text.primary.
    void setBreadcrumb(std::vector<std::wstring> parts);
    void setWindowActive(bool active);
    void setMaximized(bool maximized);

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] ui::HitZone windowZone() const override { return ui::HitZone::Caption; }

private:
    std::wstring m_appName;
    std::wstring m_maximizeName;
    std::wstring m_restoreName;
    std::vector<std::wstring> m_breadcrumb;
    bool m_windowActive = true;
    PaletteTrigger* m_palette = nullptr;
    CaptionButton* m_minimize = nullptr;
    CaptionButton* m_maximize = nullptr;
    CaptionButton* m_close = nullptr;
};

} // namespace wl::app
