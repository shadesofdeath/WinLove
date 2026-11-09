#include "ui/widgets/Button.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

using tokens::Color;

constexpr float kPaddingX = 10.0f; // D-109
constexpr float kRibbonHeight = 32.0f;
constexpr float kRibbonPadX = 7.0f;
constexpr float kIconGap = 6.0f;
constexpr float kMinWidth = 60.0f;

tokens::TypeStyle labelStyle(ButtonKind kind) {
    return kind == ButtonKind::Primary || kind == ButtonKind::Danger ? tokens::TypeStyle::BodySemi : tokens::TypeStyle::BodyStrong;
}
constexpr float kPressReleaseMs = 140.0f; // motion.md: press 80 ms, release 140 ms

} // namespace

Button::Button(ButtonKind kind, std::wstring text, std::optional<icons::Icon> icon)
    : m_kind(kind), m_text(std::move(text)), m_icon(icon) {
    setFocusable(true);
    setAccessible(AccessRole::Button, m_text);
}

std::unique_ptr<Button> Button::iconOnly(icons::Icon icon, std::wstring tooltip, ButtonKind kind) {
    auto button = std::make_unique<Button>(kind, std::wstring{}, icon);
    button->setAccessible(AccessRole::Button, tooltip);
    button->setTooltip(std::move(tooltip));
    return button;
}

void Button::setText(std::wstring text) {
    m_text = std::move(text);
    m_textWidth = -1;
    setAccessible(AccessRole::Button, m_text);
    invalidate();
}

SizeF Button::measure(SizeF /*available*/) {
    const float height = m_height > 0 ? m_height : m_kind == ButtonKind::Ribbon ? kRibbonHeight : tokens::size::control;
    if (isIconOnly()) {
        return {height, height};
    }
    if (m_kind == ButtonKind::Ribbon) {
        if (m_textWidth < 0 && host()) {
            m_textWidth = textWidth(m_text, tokens::TypeStyle::Body);
        }
        return {std::ceil(2 * kRibbonPadX + std::max(m_textWidth, 0.0f) + (m_icon ? tokens::size::icon + kIconGap : 0.0f)), height};
    }
    const auto style = labelStyle(m_kind);
    if (m_textWidth < 0 && host()) {
        m_textWidth = textWidth(m_text, style);
    }
    float width = kPaddingX * 2 + std::max(m_textWidth, 0.0f);
    if (m_icon) {
        width += tokens::size::icon + kIconGap - 4;
    }
    return {std::max(width, kMinWidth), height};
}

void Button::onHoverChanged(bool hovered) {
    if (m_hover.animateTo(hovered ? 1.0f : 0.0f, tokens::motion::fastMs)) {
        animate();
    }
    invalidate();
}

void Button::onPressedChanged(bool pressed) {
    if (m_press.animateTo(pressed ? 1.0f : 0.0f, pressed ? tokens::motion::fastMs : kPressReleaseMs)) {
        animate();
    }
    invalidate();
}

bool Button::tick(double now) {
    const bool hover = m_hover.tick(now);
    const bool press = m_press.tick(now);
    invalidate();
    return hover || press;
}

void Button::onClick() {
    if (!enabled()) {
        return;
    }
    // Run a copy: the handler may destroy this button (a dialog closing itself) and with it
    // the std::function that is executing.
    if (auto invoke = onInvoke) {
        invoke();
    }
}

bool Button::onKeyDown(const KeyEvent& key) {
    return activateOnKey(key);
}

void Button::paint(Canvas& canvas) {
    const RectF b = bounds();
    const float radius = isIconOnly() ? tokens::radius::r2 : tokens::radius::button;
    // Forced gallery states win over the (idle) tweens.
    const float h = hovered() ? std::max(m_hover.value(), m_hover.running() ? 0.0f : 1.0f) : m_hover.value();
    const float p = pressed() ? std::max(m_press.value(), m_press.running() ? 0.0f : 1.0f) : m_press.value();

    Color text = Color::TextPrimary;
    auto overlay = [&]() {
        // Transparent at rest: the hover overlay fades in, pressing deepens it.
        if (h > 0 || p > 0) {
            canvas.fillRoundRect(b, radius, Ink(Color::BgHover, Color::BgHover, 0, std::min(1.0f, h + p)));
        }
        if (p > 0) {
            canvas.fillRoundRect(b, radius, Ink(Color::BgHover, Color::BgHover, 0, p));
        }
    };

    switch (m_kind) {
    case ButtonKind::Primary:
        canvas.fillRoundRect(b, radius, p > 0 ? Ink(Color::AccentHover, Color::AccentPressed, p)
                                              : Ink(Color::AccentBase, Color::AccentHover, h));
        text = Color::TextOnAccent;
        break;
    case ButtonKind::Secondary:
        // D-109: the reference tool's push button — the input fill, a strong line, pressed darker.
        canvas.fillRoundRect(b, radius, p > 0 ? Ink(Color::BgRaised, Color::BgPressed, p) : Ink(Color::BgInput, Color::BgPressed, h));
        canvas.strokeRoundRect(b, radius, Color::LineStrong);
        break;
    case ButtonKind::Subtle:
    case ButtonKind::Ghost:
    case ButtonKind::Ribbon:
        overlay();
        break;
    case ButtonKind::Danger: {
        canvas.fillRoundRect(b, radius, Color::CaptionClose);
        text = Color::TextOnAccent;
        if (h > 0) {
            canvas.fillRoundRect(b, radius, Ink(Color::TextOnAccent, Color::TextOnAccent, 0, 0.08f * h));
        }
        if (p > 0) {
            canvas.fillRoundRect(b, radius, Ink(Color::ShadowKey, Color::ShadowKey, 0, 0.25f * p));
        }
        break;
    }
    }

    Ink ink = text;
    if (m_kind == ButtonKind::Subtle) {
        ink = Ink(Color::TextSecondary, Color::TextPrimary, std::max(h, p));
    } else if (m_kind == ButtonKind::Ghost) {
        ink = Color::AccentText;
    }

    if (m_kind == ButtonKind::Ribbon) {
        float x = m_text.empty() ? b.x + std::round((b.width - tokens::size::icon) / 2) : b.x + kRibbonPadX;
        if (m_icon) {
            canvas.drawIcon(*m_icon, {x, b.y + std::round((b.height - tokens::size::icon) / 2)}, Color::AccentBase);
            x += tokens::size::icon + kIconGap;
        }
        if (!m_text.empty()) {
            canvas.drawText(m_text, {x, b.y, std::max(b.right() - x, 0.0f), b.height}, tokens::TypeStyle::Body, Color::TextPrimary);
        }
        return;
    }
    if (isIconOnly()) {
        const float s = tokens::size::icon;
        canvas.drawIcon(*m_icon, {b.x + std::round((b.width - s) / 2), b.y + std::round((b.height - s) / 2)}, ink);
        return;
    }
    const auto style = labelStyle(m_kind);
    // Content (icon + gap + text) centered as a group. A button laid out at a fixed width was never
    // measured: without its text width the text started at the centre (it looked right-aligned).
    if (m_textWidth < 0 && host()) {
        m_textWidth = textWidth(m_text, style);
    }
    const float textWidth = std::max(m_textWidth, 0.0f);
    const float content = textWidth + (m_icon ? tokens::size::icon + kIconGap : 0.0f);
    const float padding = m_icon ? kPaddingX - 2 : kPaddingX;
    float x = b.x + std::max(padding, std::round((b.width - content) / 2));
    if (m_icon) {
        canvas.drawIcon(*m_icon, {x, b.y + std::round((b.height - tokens::size::icon) / 2)}, ink);
        x += tokens::size::icon + kIconGap;
    }
    canvas.drawText(m_text, {x, b.y, b.right() - padding - x + 1, b.height}, style, ink);
}

} // namespace wl::ui
