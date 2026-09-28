#pragma once
// The drawing API every widget uses. Coordinates in DIPs; colors only as tokens (no literals).
// 1px lines are drawn as rectangles one *physical* pixel thick, snapped to the pixel grid, so
// they stay sharp at every scale (d2d-rendering-guide.md).
#include "ui/Geometry.h"
#include "ui/icons/IconCache.h"
#include "ui/text/TextStyles.h"
#include "ui/theme/Palette.h"

#include <span>
#include <string_view>

namespace wl::ui {

// A token color, optionally mixed toward a second token (hover/press transitions animate `t`).
struct Ink {
    tokens::Color from;
    tokens::Color to;
    float t = 0.0f;
    float opacity = 1.0f;

    Ink(tokens::Color color) noexcept : from(color), to(color) {} // implicit on purpose: Color -> Ink
    Ink(tokens::Color a, tokens::Color b, float mix, float alpha = 1.0f) noexcept
        : from(a), to(b), t(mix), opacity(alpha) {}
    [[nodiscard]] Ink withOpacity(float alpha) const noexcept { return {from, to, t, opacity * alpha}; }
};

class Canvas {
public:
    Canvas(ID2D1DeviceContext2* context, ThemeKind theme, float scale, const TextStyles& text, IconCache& icons);

    [[nodiscard]] ThemeKind theme() const noexcept { return m_theme; }
    [[nodiscard]] float scale() const noexcept { return m_scale; }
    [[nodiscard]] const TextStyles& text() const noexcept { return m_text; }
    [[nodiscard]] Rgba resolve(Ink ink) const noexcept;

    void clear(tokens::Color color);
    void fillRect(RectF rect, Ink ink);
    void fillRoundRect(RectF rect, float radius, Ink ink);
    // Border drawn inside `rect`, `widthPx` in physical pixels.
    void strokeRoundRect(RectF rect, float radius, Ink ink, float widthPx = 1.0f);
    void hairlineH(float x, float y, float width, Ink ink);  // 1 physical px, below y
    void hairlineV(float x, float y, float height, Ink ink); // 1 physical px, right of x
    // Straight line in DIPs with round caps (glyph drawing, e.g. caption buttons).
    void line(PointF from, PointF to, Ink ink, float widthPx = 1.0f);
    void fillEllipse(PointF center, float radius, Ink ink);
    // Elevation shadow (tokens::elevation::menu/dialog/toast) under a rounded rect.
    void dropShadow(RectF rect, float radius, std::span<const tokens::Shadow> layers);

    void drawText(std::wstring_view text, RectF rect, tokens::TypeStyle style, Ink ink,
                  TextAlign align = TextAlign::Leading);
    void drawIcon(icons::Icon icon, PointF topLeft, Ink ink, IconVariant variant = IconVariant::Regular16,
                  float size = 0 /* 0 = the variant's grid size */);

    void pushClip(RectF rect);
    void popClip();
    // Everything drawn until popOpacity() is composited at `opacity` (disabled widgets: 0.45).
    void pushOpacity(float opacity);
    void popOpacity();

private:
    ID2D1SolidColorBrush* brush(Ink ink);
    [[nodiscard]] float px() const noexcept { return 1.0f / m_scale; }

    ID2D1DeviceContext2* m_context;
    ThemeKind m_theme;
    float m_scale;
    const TextStyles& m_text;
    IconCache& m_icons;
    ComPtr<ID2D1SolidColorBrush> m_brush;
};

} // namespace wl::ui
