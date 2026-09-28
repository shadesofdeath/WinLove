#pragma once
// Title bar per 03_components/statusbar-titlebar.md: brand, command palette trigger, caption buttons.
// Faz 1.1–1.3 version: draws and hit-tests itself directly. It becomes a composition of widgets
// (IconButton, Kbd, ...) once the widget tree lands in Faz 1.4.
#include "ui/platform/Window.h"
#include "ui/render/Canvas.h"

#include <string>

namespace wl::app {

class TitleBar {
public:
    enum class Part : std::uint8_t { None, Palette, Minimize, Maximize, Close };
    enum class Action : std::uint8_t { None, OpenPalette, Minimize, ToggleMaximize, Close };

    struct Labels {
        std::wstring appName;
        std::wstring paletteHint;
        std::wstring ctrlKey; // "Ctrl"
    };

    void setLabels(Labels labels) { m_labels = std::move(labels); }
    void setActive(bool active) noexcept { m_active = active; }
    void setMaximized(bool maximized) noexcept { m_maximized = maximized; }
    void layout(float width);

    void paint(ui::Canvas& canvas) const;
    [[nodiscard]] ui::HitZone hitTest(ui::PointF point) const;

    // Returns true when the visual state changed (repaint needed). An action, if any, is
    // reported through `action` on pointer-up over the part that was pressed.
    bool onPointer(const ui::PointerEvent& event, Action& action);

private:
    [[nodiscard]] Part partAt(ui::PointF point, ui::HitZone zone) const;
    void paintCaptionButton(ui::Canvas& canvas, Part part, ui::RectF rect) const;
    void paintPalette(ui::Canvas& canvas) const;

    Labels m_labels;
    float m_width = 0;
    ui::RectF m_palette{};
    ui::RectF m_minimize{};
    ui::RectF m_maximize{};
    ui::RectF m_close{};
    Part m_hovered = Part::None;
    Part m_pressed = Part::None;
    bool m_active = true;
    bool m_maximized = false;
};

} // namespace wl::app
