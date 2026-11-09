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

// Glyphs of the WinLoveUI package that the handoff icon set does not have (Painter::DrawIcon, 24 grid).
enum class Glyph : std::uint8_t { Sun, Moon, Reset };

// A token color, optionally mixed toward a second token (hover/press transitions animate `t`).
struct Ink {
    tokens::Color from;
    tokens::Color to;
    float t = 0.0f;
    float opacity = 1.0f;

    Ink(tokens::Color color) noexcept : from(color), to(color) {} // implicit on purpose: Color -> Ink
    Ink(tokens::Color a, tokens::Color b, float mix, float alpha = 1.0f) noexcept
        : from(a), to(b), t(mix), opacity(alpha) {}
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
    // A color that is not a token of the theme (the accent swatches of the app settings).
    void fillRoundRect(RectF rect, float radius, Rgba color);
    // Border drawn inside `rect`, `widthPx` in physical pixels.
    void strokeRoundRect(RectF rect, float radius, Ink ink, float widthPx = 1.0f);
    void hairlineH(float x, float y, float width, Ink ink);  // 1 physical px, below y
    void hairlineV(float x, float y, float height, Ink ink); // 1 physical px, right of x
    // Straight line in DIPs with round caps (glyph drawing, e.g. caption buttons).
    void line(PointF from, PointF to, Ink ink, float widthPx = 1.0f);
    void fillEllipse(PointF center, float radius, Ink ink);
    // Colors that are not tokens of the theme (a national flag): a circle and a closed polygon.
    void fillEllipse(PointF center, float radius, Rgba color);
    void fillPolygon(std::span<const PointF> points, Rgba color);
    // A drawing in another theme than the app's (a preview of a screen): colours, not tokens.
    void strokeRoundRect(RectF rect, float radius, Rgba color, float widthPx);
    void drawText(std::wstring_view text, RectF rect, tokens::TypeStyle style, Rgba color, TextAlign align = TextAlign::Leading);
    void drawIcon(icons::Icon icon, PointF topLeft, Rgba color, IconVariant variant, float size);
    // Two colours from the top-left corner to the bottom-right one (a radius of half the side: a circle).
    void fillGradient(RectF rect, float radius, Rgba from, Rgba to);
    // A soft light: the colour at the centre, fading to nothing at the ellipse's edge.
    void fillGlow(PointF center, float radiusX, float radiusY, Rgba color);
    // Elevation shadow (tokens::elevation::menu/dialog/toast) under a rounded rect.
    void dropShadow(RectF rect, float radius, std::span<const tokens::Shadow> layers);
    // A floating surface (dialog, menu, toast, tooltip, chip): shadow, overlay fill, strong line.
    void panel(RectF rect, float radius, std::span<const tokens::Shadow> elevation);

    void drawText(std::wstring_view text, RectF rect, tokens::TypeStyle style, Ink ink,
                  TextAlign align = TextAlign::Leading);
    // Word-wrapped, top-aligned; lines beyond rect.height are clipped.
    void drawTextWrapped(std::wstring_view text, RectF rect, tokens::TypeStyle style, Ink ink,
                         TextAlign align = TextAlign::Leading);
    void drawIcon(icons::Icon icon, PointF topLeft, Ink ink, IconVariant variant = IconVariant::Regular16,
                  float size = 0 /* 0 = the variant's grid size */, float rotationDegrees = 0);
    // A package glyph scaled into `box` (square), stroke in DIPs, round caps (D-107).
    void drawGlyph(Glyph glyph, RectF box, Ink ink, float strokeDip = 1.8f);
    // 2px progress track (line.strong) with a fill (progress-skeleton-empty.md "ProgressBar").
    void progressBar(RectF track, float fraction, Ink fill = tokens::Color::AccentBase);
    // An icon out of a file (.ico, .dll, .exe; `index` as PrivateExtractIcons takes it: >= 0 the
    // n-th icon, < 0 a resource id), drawn into `rect` at its physical pixel size. Loaded once per
    // file / index / size and kept (UI thread) until forgetFileIcons(). False when the file has no such icon.
    bool drawFileIcon(const std::wstring& file, int index, RectF rect, float opacity = 1.0f);
    // Drops what drawFileIcon() keeps: the files may be other ones now under the same paths (an
    // image mounted again in the same folder, after its icons were changed).
    static void forgetFileIcons();

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
