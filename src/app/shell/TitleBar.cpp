#include "app/shell/TitleBar.h"

#include <array>
#include <cmath>
#include <string_view>

namespace wl::app {

using ui::HitZone;
using ui::PointF;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;
namespace spacing = ui::tokens::spacing;

namespace {

// statusbar-titlebar.md
constexpr float kBrandPadding = 10.0f;
constexpr float kPaletteWidth = 280.0f;
constexpr float kPaletteHeight = 20.0f;
constexpr float kPaletteInset = 6.0f;
constexpr float kGlyph = 10.0f; // caption glyph box (d2d-rendering-guide.md: 10px, offset 18,11)
constexpr float kKbdHeight = 14.0f;
constexpr float kKbdPadding = 3.0f;

} // namespace

void TitleBar::layout(float width) {
    m_width = width;
    const float button = size::captionButton;
    m_close = {width - button, 0, button, size::titleBar};
    m_maximize = {m_close.x - button, 0, button, size::titleBar};
    m_minimize = {m_maximize.x - button, 0, button, size::titleBar};
    m_palette = {std::round((width - kPaletteWidth) / 2), (size::titleBar - kPaletteHeight) / 2, kPaletteWidth,
                 kPaletteHeight};
}

HitZone TitleBar::hitTest(PointF p) const {
    if (p.y >= size::titleBar) {
        return HitZone::Client;
    }
    if (m_close.contains(p)) return HitZone::CloseButton;
    if (m_maximize.contains(p)) return HitZone::MaximizeButton;
    if (m_minimize.contains(p)) return HitZone::MinimizeButton;
    if (m_palette.contains(p)) return HitZone::Client;
    return HitZone::Caption; // everything else drags the window
}

TitleBar::Part TitleBar::partAt(PointF p, HitZone zone) const {
    switch (zone) {
    case HitZone::MinimizeButton: return Part::Minimize;
    case HitZone::MaximizeButton: return Part::Maximize;
    case HitZone::CloseButton: return Part::Close;
    case HitZone::Caption: return Part::None;
    case HitZone::Client: break;
    }
    return m_palette.contains(p) ? Part::Palette : Part::None;
}

bool TitleBar::onPointer(const ui::PointerEvent& event, Action& action) {
    action = Action::None;
    const Part before = m_hovered;
    const Part pressedBefore = m_pressed;
    const Part part = event.action == ui::PointerAction::Leave ? Part::None : partAt(event.position, event.zone);

    switch (event.action) {
    case ui::PointerAction::Move:
        m_hovered = part;
        break;
    case ui::PointerAction::Leave:
        m_hovered = Part::None;
        m_pressed = Part::None;
        break;
    case ui::PointerAction::Down:
        m_hovered = part;
        m_pressed = part;
        break;
    case ui::PointerAction::Up:
        if (m_pressed != Part::None && m_pressed == part) {
            switch (part) {
            case Part::Palette: action = Action::OpenPalette; break;
            case Part::Minimize: action = Action::Minimize; break;
            case Part::Maximize: action = Action::ToggleMaximize; break;
            case Part::Close: action = Action::Close; break;
            case Part::None: break;
            }
        }
        m_pressed = Part::None;
        m_hovered = part;
        break;
    }
    return m_hovered != before || m_pressed != pressedBefore;
}

void TitleBar::paint(ui::Canvas& canvas) const {
    canvas.fillRect({0, 0, m_width, size::titleBar}, Color::BgPanel);
    canvas.hairlineH(0, size::titleBar - 1.0f / canvas.scale(), m_width, Color::LineSubtle);

    // Brand: mark 16 (accent) + 8 gap + app name (bodyStrong). Inactive window: tertiary.
    const Color ink = m_active ? Color::TextPrimary : Color::TextTertiary;
    const float markY = (size::titleBar - size::icon) / 2;
    canvas.drawIcon(ui::icons::Icon::BrandMark, {kBrandPadding, markY}, m_active ? Color::AccentBase : Color::TextTertiary);
    const float nameX = kBrandPadding + size::icon + spacing::s4;
    canvas.drawText(m_labels.appName, {nameX, 0, 120, size::titleBar}, TypeStyle::BodyStrong, ink);

    paintPalette(canvas);
    paintCaptionButton(canvas, Part::Minimize, m_minimize);
    paintCaptionButton(canvas, Part::Maximize, m_maximize);
    paintCaptionButton(canvas, Part::Close, m_close);
}

void TitleBar::paintPalette(ui::Canvas& canvas) const {
    const bool hovered = m_hovered == Part::Palette;
    canvas.fillRoundRect(m_palette, ui::tokens::radius::r2, Color::BgBase);
    canvas.strokeRoundRect(m_palette, ui::tokens::radius::r2, hovered ? Color::TextTertiary : Color::LineStrong);

    const float iconY = m_palette.y + (m_palette.height - size::icon) / 2;
    canvas.drawIcon(ui::icons::Icon::Search, {m_palette.x + kPaletteInset, iconY}, Color::TextTertiary);

    // Keycaps "Ctrl" "K", right-aligned, 6 gap (kbd: mono 10/14, 1px line.strong, r2, padding 0 3).
    const auto& text = canvas.text();
    float right = m_palette.right() - kPaletteInset;
    const float kbdY = m_palette.y + (m_palette.height - kKbdHeight) / 2;
    const std::array<std::wstring_view, 2> keys{L"K", m_labels.ctrlKey}; // laid out right to left
    for (const auto key : keys) {
        const float w = std::ceil(text.measure(key, TypeStyle::Kbd)) + 2 * kKbdPadding + 2;
        const RectF cap{right - w, kbdY, w, kKbdHeight};
        canvas.strokeRoundRect(cap, ui::tokens::radius::r1, Color::LineStrong);
        canvas.drawText(key, cap, TypeStyle::Kbd, Color::TextTertiary, ui::TextAlign::Center);
        right = cap.x - kPaletteInset / 2;
    }

    const float labelX = m_palette.x + kPaletteInset + size::icon + kPaletteInset;
    canvas.drawText(m_labels.paletteHint, {labelX, m_palette.y, right - labelX - kPaletteInset, m_palette.height},
                    TypeStyle::Caption, Color::TextTertiary);
}

void TitleBar::paintCaptionButton(ui::Canvas& canvas, Part part, RectF rect) const {
    const bool hovered = m_hovered == part;
    const bool pressed = m_pressed == part && hovered;
    Color glyph = m_active ? Color::TextSecondary : Color::TextTertiary;
    if (part == Part::Close && hovered) {
        canvas.fillRect(rect, Color::StatusError, pressed ? 0.85f : 1.0f);
        glyph = ui::bestContrast(canvas.theme(), Color::StatusError, Color::TextPrimary, Color::TextOnAccent);
    } else if (hovered) {
        canvas.fillRect(rect, pressed ? Color::BgPressed : Color::BgRaised);
        glyph = Color::TextPrimary;
    }

    // 10×10 glyph box centered in the 46×32 button, 1px strokes.
    const float gx = rect.x + std::round((rect.width - kGlyph) / 2);
    const float gy = rect.y + std::round((rect.height - kGlyph) / 2);
    const float px = 1.0f / canvas.scale();
    switch (part) {
    case Part::Minimize:
        canvas.hairlineH(gx, gy + kGlyph / 2, kGlyph, glyph);
        break;
    case Part::Maximize:
        if (m_maximized) {
            // Restore: front square plus the top/right edges of the square behind it.
            constexpr float s = 8.0f;
            canvas.strokeRoundRect({gx, gy + 2, s, s}, 0, glyph);
            canvas.hairlineH(gx + 2, gy, s, glyph);
            canvas.hairlineV(gx + 2 + s - px, gy, s, glyph);
        } else {
            canvas.strokeRoundRect({gx, gy, kGlyph, kGlyph}, 0, glyph);
        }
        break;
    case Part::Close:
        canvas.line({gx, gy}, {gx + kGlyph, gy + kGlyph}, glyph);
        canvas.line({gx + kGlyph, gy}, {gx, gy + kGlyph}, glyph);
        break;
    case Part::None:
    case Part::Palette:
        break;
    }
}

} // namespace wl::app
