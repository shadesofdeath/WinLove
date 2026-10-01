#include "app/shell/TitleBar.h"

#include "ui/widget/Host.h"
#include "ui/widgets/Kbd.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::HitZone;
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
constexpr float kSeparatorGap = 12.0f;
constexpr float kGlyph = 10.0f; // caption glyph box (d2d-rendering-guide.md)

bool isActivationKey(const ui::KeyEvent& key) {
    return key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN;
}

} // namespace

// ---- PaletteTrigger ----------------------------------------------------------------------------

PaletteTrigger::PaletteTrigger(std::wstring hint, std::vector<std::wstring> keys)
    : m_hint(std::move(hint)), m_keys(std::move(keys)) {
    setFocusable(true);
    setAccessible(ui::AccessRole::Button, m_hint);
}

void PaletteTrigger::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRoundRect(b, ui::tokens::radius::r2, Color::BgBase);
    canvas.strokeRoundRect(b, ui::tokens::radius::r2, hovered() ? Color::TextTertiary : Color::LineStrong);
    canvas.drawIcon(ui::icons::Icon::Search, {b.x + kPaletteInset, b.y + std::round((b.height - size::icon) / 2)},
                    Color::TextTertiary);
    const float keysLeft = ui::Kbd::paintKeys(canvas, m_keys, b.right() - kPaletteInset, b.y + b.height / 2);
    const float labelX = b.x + kPaletteInset + size::icon + kPaletteInset;
    canvas.drawText(m_hint, {labelX, b.y, keysLeft - labelX - kPaletteInset, b.height}, TypeStyle::Caption,
                    Color::TextTertiary);
}

void PaletteTrigger::onClick() {
    if (onInvoke) {
        onInvoke();
    }
}

bool PaletteTrigger::onKeyDown(const ui::KeyEvent& key) {
    if (isActivationKey(key)) {
        onClick();
        return true;
    }
    return false;
}

// ---- CaptionButton -----------------------------------------------------------------------------

CaptionButton::CaptionButton(Kind kind) : m_kind(kind) {
    setFocusable(true);
}

void CaptionButton::setMaximized(bool maximized) {
    m_maximized = maximized;
    invalidate();
}

void CaptionButton::setWindowActive(bool active) {
    m_windowActive = active;
    invalidate();
}

HitZone CaptionButton::windowZone() const {
    switch (m_kind) {
    case Kind::Minimize: return HitZone::MinimizeButton;
    case Kind::Maximize: return HitZone::MaximizeButton;
    case Kind::Close: return HitZone::CloseButton;
    }
    return HitZone::Client;
}

void CaptionButton::onHoverChanged(bool hovered) {
    if (m_hover.animateTo(hovered ? 1.0f : 0.0f, ui::tokens::motion::fastMs)) {
        animate();
    }
    invalidate();
}

bool CaptionButton::tick(double now) {
    const bool running = m_hover.tick(now);
    invalidate();
    return running;
}

void CaptionButton::onClick() {
    if (onInvoke) {
        onInvoke();
    }
}

bool CaptionButton::onKeyDown(const ui::KeyEvent& key) {
    if (isActivationKey(key)) {
        onClick();
        return true;
    }
    return false;
}

void CaptionButton::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float h = hovered() && !m_hover.running() ? 1.0f : m_hover.value();
    const Color rest = m_windowActive ? Color::TextSecondary : Color::TextTertiary;
    ui::Ink glyph = rest;
    if (m_kind == Kind::Close) {
        if (h > 0) {
            canvas.fillRect(b, ui::Ink(Color::StatusError, Color::StatusError, 0, pressed() ? 0.85f * h : h));
            glyph = ui::Ink(rest, ui::bestContrast(canvas.theme(), Color::StatusError, Color::TextPrimary,
                                                   Color::TextOnAccent), h);
        }
    } else if (h > 0) {
        canvas.fillRect(b, pressed() ? ui::Ink(Color::BgPressed) : ui::Ink(Color::BgRaised, Color::BgRaised, 0, h));
        glyph = ui::Ink(rest, Color::TextPrimary, h);
    }

    // 10×10 glyph box centered in the 46×32 button, 1px strokes.
    const float gx = b.x + std::round((b.width - kGlyph) / 2);
    const float gy = b.y + std::round((b.height - kGlyph) / 2);
    const float px = 1.0f / canvas.scale();
    switch (m_kind) {
    case Kind::Minimize:
        canvas.hairlineH(gx, gy + kGlyph / 2, kGlyph, glyph);
        break;
    case Kind::Maximize:
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
    case Kind::Close:
        canvas.line({gx, gy}, {gx + kGlyph, gy + kGlyph}, glyph);
        canvas.line({gx + kGlyph, gy}, {gx, gy + kGlyph}, glyph);
        break;
    }
}

// ---- TitleBar ----------------------------------------------------------------------------------

TitleBar::TitleBar(const Labels& labels)
    : m_appName(labels.appName), m_maximizeName(labels.maximize), m_restoreName(labels.restore) {
    m_palette = &add<PaletteTrigger>(labels.paletteHint, std::vector<std::wstring>{labels.ctrlKey, L"K"});
    m_minimize = &add<CaptionButton>(CaptionButton::Kind::Minimize);
    m_maximize = &add<CaptionButton>(CaptionButton::Kind::Maximize);
    m_close = &add<CaptionButton>(CaptionButton::Kind::Close);
    // No tooltip on maximize: Windows 11 shows the Snap Layouts flyout there.
    m_minimize->setTooltip(labels.minimize);
    m_close->setTooltip(labels.close);
    m_minimize->setAccessible(ui::AccessRole::Button, labels.minimize);
    m_close->setAccessible(ui::AccessRole::Button, labels.close);
    m_maximize->setAccessible(ui::AccessRole::Button, labels.maximize);
}

void TitleBar::setBreadcrumb(std::vector<std::wstring> parts) {
    m_breadcrumb = std::move(parts);
    invalidate();
}

void TitleBar::setWindowActive(bool active) {
    m_windowActive = active;
    for (auto* button : {m_minimize, m_maximize, m_close}) {
        button->setWindowActive(active);
    }
    invalidate();
}

void TitleBar::setMaximized(bool maximized) {
    m_maximize->setMaximized(maximized);
    m_maximize->setAccessible(ui::AccessRole::Button, maximized ? m_restoreName : m_maximizeName);
}

void TitleBar::layout() {
    const RectF b = bounds();
    const float button = size::captionButton;
    m_close->setBounds({b.right() - button, b.y, button, b.height});
    m_maximize->setBounds({m_close->bounds().x - button, b.y, button, b.height});
    m_minimize->setBounds({m_maximize->bounds().x - button, b.y, button, b.height});
    m_palette->setBounds({b.x + std::round((b.width - kPaletteWidth) / 2), b.y + (b.height - kPaletteHeight) / 2,
                          kPaletteWidth, kPaletteHeight});
}

void TitleBar::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineH(b.x, b.bottom() - 1.0f / canvas.scale(), b.width, Color::LineSubtle);

    // Brand: mark 16 (accent) + 8 gap + app name (bodyStrong). Inactive window: tertiary.
    const Color ink = m_windowActive ? Color::TextPrimary : Color::TextTertiary;
    canvas.drawIcon(ui::icons::Icon::BrandMark, {b.x + kBrandPadding, b.y + (b.height - size::icon) / 2},
                    m_windowActive ? Color::AccentBase : Color::TextTertiary);
    const float nameX = b.x + kBrandPadding + size::icon + spacing::s4;
    const float nameWidth = std::ceil(canvas.text().measure(m_appName, TypeStyle::BodyStrong));
    canvas.drawText(m_appName, {nameX, b.y, nameWidth + 1, b.height}, TypeStyle::BodyStrong, ink);

    // Breadcrumb after a 1×12 separator (line.strong): items text.secondary, last text.primary,
    // "›" separators text.tertiary with 6px gaps; long items end in an ellipsis (max 180).
    if (!m_breadcrumb.empty()) {
        constexpr float kCrumbGap = 6.0f;
        constexpr float kCrumbMax = 180.0f;
        const float sepX = nameX + nameWidth + kSeparatorGap;
        canvas.hairlineV(sepX, b.y + (b.height - 12) / 2, 12, Color::LineStrong);
        float x = sepX + kSeparatorGap;
        const float maxRight = m_palette->bounds().x - kSeparatorGap;
        for (std::size_t i = 0; i < m_breadcrumb.size() && x < maxRight; ++i) {
            const bool last = i + 1 == m_breadcrumb.size();
            if (i > 0) {
                const float arrow = std::ceil(canvas.text().measure(L"\u203A", TypeStyle::Body));
                canvas.drawText(L"\u203A", {x, b.y, arrow + 1, b.height}, TypeStyle::Body, Color::TextTertiary);
                x += arrow + kCrumbGap;
            }
            const float width = std::min({std::ceil(canvas.text().measure(m_breadcrumb[i], TypeStyle::Body)) + 1,
                                          kCrumbMax, std::max(maxRight - x, 0.0f)});
            const Color crumbInk = !m_windowActive ? Color::TextTertiary : last ? Color::TextPrimary : Color::TextSecondary;
            canvas.drawText(m_breadcrumb[i], {x, b.y, width, b.height}, TypeStyle::Body, crumbInk);
            x += width + kCrumbGap;
        }
    }
}

} // namespace wl::app
