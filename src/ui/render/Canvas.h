#pragma once
// The drawing API every widget uses. Coordinates in DIPs; colors only as tokens (no literals).
// 1px lines are drawn as rectangles one *physical* pixel thick, snapped to the pixel grid, so
// they stay sharp at every scale (d2d-rendering-guide.md).
#include "ui/Geometry.h"
#include "ui/icons/IconCache.h"
#include "ui/text/TextStyles.h"
#include "ui/theme/Palette.h"

#include <string_view>

namespace wl::ui {

class Canvas {
public:
    Canvas(ID2D1DeviceContext2* context, ThemeKind theme, float scale, const TextStyles& text, IconCache& icons);

    [[nodiscard]] ThemeKind theme() const noexcept { return m_theme; }
    [[nodiscard]] float scale() const noexcept { return m_scale; }
    [[nodiscard]] const TextStyles& text() const noexcept { return m_text; }

    void clear(tokens::Color color);
    void fillRect(RectF rect, tokens::Color color, float opacity = 1.0f);
    void fillRoundRect(RectF rect, float radius, tokens::Color color, float opacity = 1.0f);
    // Border drawn inside `rect`, `width` in physical pixels.
    void strokeRoundRect(RectF rect, float radius, tokens::Color color, float widthPx = 1.0f);
    void hairlineH(float x, float y, float width, tokens::Color color); // 1 physical px, below y
    void hairlineV(float x, float y, float height, tokens::Color color); // 1 physical px, right of x
    // Straight line in DIPs with round caps (glyph drawing, e.g. caption buttons).
    void line(PointF from, PointF to, tokens::Color color, float widthPx = 1.0f);
    void fillEllipse(PointF center, float radius, tokens::Color color);

    void drawText(std::wstring_view text, RectF rect, tokens::TypeStyle style, tokens::Color color,
                  TextAlign align = TextAlign::Leading);
    void drawIcon(icons::Icon icon, PointF topLeft, tokens::Color color, IconVariant variant = IconVariant::Regular16,
                  float size = 0 /* 0 = the variant's grid size */);

    void pushClip(RectF rect);
    void popClip();

private:
    ID2D1SolidColorBrush* brush(tokens::Color color, float opacity = 1.0f);
    [[nodiscard]] float px() const noexcept { return 1.0f / m_scale; }

    ID2D1DeviceContext2* m_context;
    ThemeKind m_theme;
    float m_scale;
    const TextStyles& m_text;
    IconCache& m_icons;
    ComPtr<ID2D1SolidColorBrush> m_brush;
};

} // namespace wl::ui
