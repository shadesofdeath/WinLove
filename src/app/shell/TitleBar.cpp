#include "app/shell/TitleBar.h"

#include "ui/widget/Host.h"
#include "ui/widgets/Kbd.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace wl::app {

using ui::HitZone;
using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {

// docs/LAYOUT.md (design/WinLoveUI): logo 18 at x 20, name at 47, menus 30 high with 10px padding
// and 2 apart, the palette box centered (at most 440×34; 380 kept free on each side).
constexpr float kMenusGap = 26.0f; // D-109: the reference tool's first tab starts ~66 in, after the ≡ button
constexpr float kMenuPadX = 12.0f;
constexpr float kMenuIconGap = ui::tokens::spacing::s3; // a menu's icon → its label
constexpr float kMenuGap = 4.0f;
constexpr float kPaletteIconX = 10.0f;
constexpr float kPaletteTextX = 32.0f;
constexpr float kGlyph = 10.0f;      // minimize / maximize / close glyph box
constexpr float kThemeGlyph = 15.0f; // sun / moon
constexpr float kTitleClearLeft = 16.0f;  // the title keeps this far from the last menu
constexpr float kTitleClearRight = 8.0f;  // and from the theme button (its glyph sits 12 further in)
constexpr float kTitleLeast = 40.0f;      // narrower than this the title is left out

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
    // Painter::SearchBox: panel fill, line border (strong on hover), search glyph, placeholder,
    // the shortcut in a bordered box on the right.
    const RectF b = bounds();
    canvas.fillRoundRect(b, ui::tokens::radius::r2, Color::BgInput);
    canvas.strokeRoundRect(b, ui::tokens::radius::r2, hovered() ? Color::LineStrong : Color::LineSubtle);
    canvas.drawIcon(ui::icons::Icon::Search, {b.x + kPaletteIconX, b.y + std::round((b.height - size::icon) / 2)},
                    Color::TextTertiary);
    const float keysLeft = ui::Kbd::paintKeys(canvas, m_keys, b.right() - 8, b.y + b.height / 2);
    const float labelX = b.x + kPaletteTextX;
    canvas.drawText(m_hint, {labelX, b.y, std::max(keysLeft - 8 - labelX, 0.0f), b.height}, TypeStyle::Body,
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

// ---- MenuButton --------------------------------------------------------------------------------

MenuButton::MenuButton(std::wstring label, std::optional<ui::icons::Icon> icon) : m_label(std::move(label)), m_icon(icon) {
    setFocusable(true);
    setAccessible(ui::AccessRole::TabItem, m_label);
}

void MenuButton::setActive(bool active) {
    if (active != m_active) {
        m_active = active;
        invalidate();
    }
}

ui::SizeF MenuButton::measure(ui::SizeF /*available*/) {
    m_textWidth = std::ceil(textWidth(m_label, TypeStyle::Body, 60.0f)); // kept for paint()
    const float icon = m_icon ? size::icon + kMenuIconGap : 0.0f;
    return {icon + m_textWidth + 2 * kMenuPadX, size::titleBar};
}

void MenuButton::paint(ui::Canvas& canvas) {
    // D-109: the reference tool's ribbon tabs — text only; the chosen one in text.primary over a 2px accent
    // line as wide as its text, the others text.secondary (text.primary on hover).
    // 2026-11-11: the workspace's icon before it, in its own colour (the navigation's look).
    const RectF b = bounds();
    const float icon = m_icon ? size::icon + kMenuIconGap : 0.0f;
    const float contentW = std::min(icon + m_textWidth, b.width);
    const float x = b.x + std::round((b.width - contentW) / 2);
    if (m_icon) {
        canvas.drawIcon(*m_icon, {x, b.y + std::round((b.height - size::icon) / 2)},
                        m_active || hovered() ? ui::Ink(Color::AccentBase) : ui::Ink(Color::TextSecondary));
    }
    canvas.drawText(m_label, {x + icon, b.y, std::max(b.right() - x - icon, 0.0f), b.height}, TypeStyle::Body,
                    m_active || hovered() ? Color::TextPrimary : Color::TextSecondary);
    if (m_active) {
        canvas.fillRoundRect({x, b.bottom() - 5, contentW, 2}, 1.0f, Color::AccentBase);
    }
}

RectF MenuButton::focusRect() const {
    // The Host draws the ring focusOffset + 1 outside this: 1 DIP clear of the bar's top and bottom,
    // the accent line under the label inside it.
    const RectF b = bounds();
    const float inset = ui::tokens::radius::focusOffset + 2;
    return {b.x, b.y + inset, b.width, std::max(b.height - 2 * inset, 0.0f)};
}

void MenuButton::onClick() {
    if (auto invoke = onInvoke) {
        invoke();
    }
}

bool MenuButton::onKeyDown(const ui::KeyEvent& key) {
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
    case Kind::Theme:
    case Kind::Menu: break;
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

RectF CaptionButton::focusRect() const {
    const float inset = ui::tokens::radius::focusOffset + 1;
    const RectF b = bounds();
    return {b.x + inset, b.y + inset, std::max(b.width - 2 * inset, 0.0f), std::max(b.height - 2 * inset, 0.0f)};
}

void CaptionButton::onClick() {
    if (auto invoke = onInvoke) {
        invoke();
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
    // Painter::CaptionButton: hover overlay; close turns caption.close with a white glyph.
    const RectF b = bounds();
    const float h = hovered() && !m_hover.running() ? 1.0f : m_hover.value();
    const Color rest = m_windowActive ? Color::TextSecondary : Color::TextTertiary;
    ui::Ink glyph = rest;
    if (m_kind == Kind::Close) {
        if (h > 0) {
            canvas.fillRect(b, ui::Ink(Color::CaptionClose, Color::CaptionClose, 0, pressed() ? 0.85f * h : h));
            glyph = ui::Ink(rest, Color::TextOnAccent, h);
        }
    } else if (h > 0) {
        canvas.fillRect(b, ui::Ink(Color::BgHover, Color::BgHover, 0, pressed() ? 1.0f : h));
        if (pressed()) {
            canvas.fillRect(b, Color::BgHover);
        }
        glyph = ui::Ink(rest, Color::TextPrimary, h);
    }

    if (m_kind == Kind::Menu) {
        // ≡: three 14-wide strokes, 5 apart.
        const float w = 14.0f;
        const float x0 = b.x + std::round((b.width - w) / 2);
        const float cy = b.y + std::round(b.height / 2);
        for (const float dy : {-5.0f, 0.0f, 5.0f}) {
            canvas.hairlineH(x0, cy + dy, w, glyph);
        }
        return;
    }
    if (m_kind == Kind::Theme) {
        // The sun in the dark theme (to light), the moon in the light one.
        const float s = kThemeGlyph;
        const RectF box{b.x + std::round((b.width - s) / 2), b.y + std::round((b.height - s) / 2), s, s};
        // 1.1 DIP like the caption buttons' fine strokes (was 1.6; the user, 2026-11-09: "thinner").
        canvas.drawGlyph(canvas.theme() == ui::ThemeKind::Light ? ui::Glyph::Moon : ui::Glyph::Sun, box, glyph, 1.1f);
        return;
    }
    // 10×10 glyph box centered in the 46×44 button, 1px strokes.
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
            canvas.strokeRoundRect({gx, gy + 2, s, s}, 1.5f, glyph);
            canvas.hairlineH(gx + 2, gy, s, glyph);
            canvas.hairlineV(gx + 2 + s - px, gy, s, glyph);
        } else {
            canvas.strokeRoundRect({gx, gy, kGlyph, kGlyph}, 1.5f, glyph);
        }
        break;
    case Kind::Close:
        canvas.line({gx, gy}, {gx + kGlyph, gy + kGlyph}, glyph);
        canvas.line({gx + kGlyph, gy}, {gx, gy + kGlyph}, glyph);
        break;
    case Kind::Theme:
    case Kind::Menu: break;
    }
}

// ---- TitleBar ----------------------------------------------------------------------------------

TitleBar::TitleBar(const Labels& labels)
    : m_appName(labels.appName), m_maximizeName(labels.maximize), m_restoreName(labels.restore) {
    // D-147: the wordmark's second word (from its second capital: "Win" + "Love") takes the accent.
    for (std::size_t i = 1; i < m_appName.size(); ++i) {
        if (std::iswupper(m_appName[i])) {
            m_accentFrom = i;
            break;
        }
    }
    for (std::size_t i = 0; i < labels.menus.size(); ++i) {
        auto& menu = add<MenuButton>(labels.menus[i], i < labels.menuIcons.size() ? std::optional(labels.menuIcons[i]) : std::nullopt);
        menu.onInvoke = [this, i] {
            if (onMenu) {
                onMenu(static_cast<int>(i));
            }
        };
        m_menus.push_back(&menu);
    }
    m_palette = &add<PaletteTrigger>(labels.paletteHint, std::vector<std::wstring>{labels.ctrlKey, L"K"});
    m_palette->setVisible(false); // D-109: no box in the bar (Ctrl K opens the palette)
    m_appMenu = &add<CaptionButton>(CaptionButton::Kind::Menu);
    m_appMenu->setTooltip(labels.appMenu);
    m_appMenu->setAccessible(ui::AccessRole::Button, labels.appMenu);
    m_theme = &add<CaptionButton>(CaptionButton::Kind::Theme);
    m_minimize = &add<CaptionButton>(CaptionButton::Kind::Minimize);
    m_maximize = &add<CaptionButton>(CaptionButton::Kind::Maximize);
    m_close = &add<CaptionButton>(CaptionButton::Kind::Close);
    // No tooltip on maximize: Windows 11 shows the Snap Layouts flyout there.
    m_theme->setTooltip(labels.theme);
    m_minimize->setTooltip(labels.minimize);
    m_close->setTooltip(labels.close);
    m_theme->setAccessible(ui::AccessRole::Button, labels.theme);
    m_minimize->setAccessible(ui::AccessRole::Button, labels.minimize);
    m_close->setAccessible(ui::AccessRole::Button, labels.close);
    m_maximize->setAccessible(ui::AccessRole::Button, labels.maximize);
}

void TitleBar::setActiveMenu(int menu) {
    for (std::size_t i = 0; i < m_menus.size(); ++i) {
        m_menus[i]->setActive(static_cast<int>(i) == menu);
    }
}

void TitleBar::setWindowActive(bool active) {
    m_windowActive = active;
    for (auto* button : {m_theme, m_appMenu, m_minimize, m_maximize, m_close}) {
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
    m_theme->setBounds({m_minimize->bounds().x - button, b.y, button, b.height});
    m_appMenu->setBounds({b.x, b.y, button, b.height});

    float x = b.x + button + kMenusGap - kMenuPadX;
    m_menusLeft = x;
    float menusRight = x;
    for (auto* menu : m_menus) {
        const ui::SizeF s = menu->measure({});
        menu->setBounds({x, b.y, s.width, b.height});
        menusRight = menu->bounds().right();
        x += s.width + kMenuGap;
    }
    m_palette->setBounds({x, b.y, 0, 0});
    // The window title (the reference tool centres it in the window); measured here, not in paint().
    m_titleWidth = textWidth(m_appName, TypeStyle::Wordmark);
    m_leadWidth = m_accentFrom ? textWidth(std::wstring_view(m_appName).substr(0, m_accentFrom), TypeStyle::Wordmark) : 0;
    m_titleRect = titleRectFor(b, m_titleWidth + 2, menusRight + kTitleClearLeft, m_theme->bounds().x - kTitleClearRight);
}

RectF TitleBar::titleRectFor(RectF bar, float textWidth, float leftLimit, float rightLimit) {
    float left = std::max(bar.x + std::round((bar.width - textWidth) / 2), leftLimit);
    if (left + textWidth > rightLimit) {
        left = std::max(rightLimit - textWidth, leftLimit); // off centre before it is cut
    }
    const float room = rightLimit - left;
    if (room < kTitleLeast || textWidth <= 0) {
        return {};
    }
    return {left, bar.y, std::min(textWidth, room), bar.height};
}

void TitleBar::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgTitleBar);
    if (m_titleRect.width <= 0) {
        return;
    }
    // D-147: the wordmark. Inactive window: dimmed, one colour. High contrast: the text colour only.
    const Color text = m_windowActive ? Color::TextPrimary : Color::TextTertiary;
    const bool twoTone = m_accentFrom > 0 && m_windowActive && canvas.theme() != ui::ThemeKind::HighContrast &&
                         !ui::systemHighContrast() && m_titleRect.width >= m_titleWidth;
    if (!twoTone) {
        // Cut with "…" (the text format's trimming) when the window is too narrow for all of it.
        canvas.drawText(m_appName, m_titleRect, TypeStyle::Wordmark, text);
        return;
    }
    const std::wstring_view name = m_appName;
    canvas.drawText(name.substr(0, m_accentFrom), {m_titleRect.x, m_titleRect.y, m_leadWidth + 1, m_titleRect.height},
                    TypeStyle::Wordmark, text);
    canvas.drawText(name.substr(m_accentFrom),
                    {m_titleRect.x + m_leadWidth, m_titleRect.y, m_titleRect.width - m_leadWidth, m_titleRect.height},
                    TypeStyle::Wordmark, Color::AccentText);
}

} // namespace wl::app
