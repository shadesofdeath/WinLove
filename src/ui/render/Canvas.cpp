#include "ui/render/Canvas.h"

#include <algorithm>

namespace wl::ui {

namespace {

D2D1_RECT_F toD2D(RectF r) {
    return D2D1::RectF(r.x, r.y, r.right(), r.bottom());
}

} // namespace

Canvas::Canvas(ID2D1DeviceContext2* context, ThemeKind theme, float scale, const TextStyles& text, IconCache& icons)
    : m_context(context), m_theme(theme), m_scale(scale), m_text(text), m_icons(icons) {
    m_context->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &m_brush);
}

ID2D1SolidColorBrush* Canvas::brush(tokens::Color color, float opacity) {
    const Rgba c = ui::color(m_theme, color);
    m_brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
    m_brush->SetOpacity(opacity);
    return m_brush.Get();
}

void Canvas::clear(tokens::Color color) {
    const Rgba c = ui::color(m_theme, color);
    m_context->Clear(D2D1::ColorF(c.r, c.g, c.b, c.a));
}

void Canvas::fillRect(RectF rect, tokens::Color color, float opacity) {
    m_context->FillRectangle(toD2D(rect), brush(color, opacity));
}

void Canvas::fillRoundRect(RectF rect, float radius, tokens::Color color, float opacity) {
    if (radius <= 0) {
        fillRect(rect, color, opacity);
        return;
    }
    m_context->FillRoundedRectangle(D2D1::RoundedRect(toD2D(rect), radius, radius), brush(color, opacity));
}

void Canvas::strokeRoundRect(RectF rect, float radius, tokens::Color color, float widthPx) {
    // Snap the outer edge to pixels, then inset half the stroke so the line lands on whole pixels.
    const float w = widthPx * px();
    const RectF snapped{snap(rect.x, m_scale), snap(rect.y, m_scale), snap(rect.width, m_scale),
                        snap(rect.height, m_scale)};
    const RectF inner = snapped.inset(w / 2, w / 2);
    const float r = std::max(radius - w / 2, 0.0f);
    m_context->DrawRoundedRectangle(D2D1::RoundedRect(toD2D(inner), r, r), brush(color), w);
}

void Canvas::hairlineH(float x, float y, float width, tokens::Color color) {
    fillRect({snap(x, m_scale), snap(y, m_scale), snap(width, m_scale), px()}, color);
}

void Canvas::hairlineV(float x, float y, float height, tokens::Color color) {
    fillRect({snap(x, m_scale), snap(y, m_scale), px(), snap(height, m_scale)}, color);
}

void Canvas::line(PointF from, PointF to, tokens::Color color, float widthPx) {
    m_context->DrawLine({from.x, from.y}, {to.x, to.y}, brush(color), widthPx * px(), m_icons.strokeStyle());
}

void Canvas::fillEllipse(PointF center, float radius, tokens::Color color) {
    m_context->FillEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), brush(color));
}

void Canvas::drawText(std::wstring_view text, RectF rect, tokens::TypeStyle style, tokens::Color color,
                      TextAlign align) {
    auto layout = m_text.layout(text, style, rect.width, rect.height, align);
    if (!layout) {
        return;
    }
    m_context->DrawTextLayout({rect.x, rect.y}, layout->Get(), brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Canvas::drawIcon(icons::Icon icon, PointF topLeft, tokens::Color color, IconVariant variant, float size) {
    const auto entry = m_icons.get(icon, variant);
    if (!entry.geometry) {
        return;
    }
    const float target = size > 0 ? size : entry.gridSize;
    const float k = target / entry.gridSize;
    D2D1_MATRIX_3X2_F previous;
    m_context->GetTransform(&previous);
    m_context->SetTransform(D2D1::Matrix3x2F::Scale(k, k) *
                            D2D1::Matrix3x2F::Translation(snap(topLeft.x, m_scale), snap(topLeft.y, m_scale)) *
                            previous);
    auto* b = brush(color);
    if (entry.filled) {
        m_context->FillGeometry(entry.geometry, b);
    }
    m_context->DrawGeometry(entry.geometry, b, entry.strokeWidth, m_icons.strokeStyle());
    m_context->SetTransform(previous);
}

void Canvas::pushClip(RectF rect) {
    m_context->PushAxisAlignedClip(toD2D(rect), D2D1_ANTIALIAS_MODE_ALIASED);
}

void Canvas::popClip() {
    m_context->PopAxisAlignedClip();
}

} // namespace wl::ui
