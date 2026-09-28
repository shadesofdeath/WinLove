#include "ui/widgets/Button.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

using tokens::Color;

constexpr float kPaddingX = 8.0f;  // button.md: padding 0 8
constexpr float kIconGap = 6.0f;
constexpr float kMinWidth = 56.0f;
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
    if (isIconOnly()) {
        return {m_height, m_height};
    }
    const auto style = m_kind == ButtonKind::Primary || m_kind == ButtonKind::Danger ? tokens::TypeStyle::BodyStrong
                                                                                      : tokens::TypeStyle::Body;
    if (m_textWidth < 0 && host()) {
        m_textWidth = std::ceil(host()->text().measure(m_text, style));
    }
    float width = kPaddingX * 2 + std::max(m_textWidth, 0.0f);
    if (m_icon) {
        width += tokens::size::icon + kIconGap;
    }
    return {std::max(width, kMinWidth), m_height};
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
    if (key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN) {
        onClick();
        return true;
    }
    return false;
}

void Button::paint(Canvas& canvas) {
    const RectF b = bounds();
    const float radius = tokens::radius::r2;
    // Forced gallery states win over the (idle) tweens.
    const float h = hovered() ? std::max(m_hover.value(), m_hover.running() ? 0.0f : 1.0f) : m_hover.value();
    const float p = pressed() ? std::max(m_press.value(), m_press.running() ? 0.0f : 1.0f) : m_press.value();

    Color text = Color::TextPrimary;
    auto fill = [&](Color rest, Color hover, Color press) {
        canvas.fillRoundRect(b, radius, p > 0 ? Ink(hover, press, p) : Ink(rest, hover, h));
    };
    auto overlay = [&](Color hover, Color press) {
        // Transparent at rest: fade the hover color in, then move toward the pressed color.
        if (h > 0 || p > 0) {
            canvas.fillRoundRect(b, radius, p > 0 ? Ink(hover, press, p) : Ink(hover, hover, 0, h));
        }
    };

    switch (m_kind) {
    case ButtonKind::Primary:
        fill(Color::AccentBase, Color::AccentHover, Color::AccentPressed);
        text = Color::TextOnAccent;
        break;
    case ButtonKind::Secondary:
        overlay(Color::BgRaised, Color::BgPressed);
        canvas.strokeRoundRect(b, radius, Color::LineStrong);
        break;
    case ButtonKind::Subtle:
        overlay(Color::BgRaised, Color::BgPressed);
        break;
    case ButtonKind::Danger: {
        canvas.fillRoundRect(b, radius, Color::StatusError);
        text = bestContrast(canvas.theme(), Color::StatusError, Color::TextPrimary, Color::TextOnAccent);
        // button.md: hover = 6% light mix, pressed = 10% darker, both opaque results.
        if (h > 0) {
            canvas.fillRoundRect(b, radius, Ink(text, text, 0, tokens::opacity::hoverOverlay * h));
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
    }

    if (isIconOnly()) {
        const float s = tokens::size::icon;
        canvas.drawIcon(*m_icon, {b.x + std::round((b.width - s) / 2), b.y + std::round((b.height - s) / 2)}, ink);
        return;
    }
    const auto style = m_kind == ButtonKind::Primary || m_kind == ButtonKind::Danger ? tokens::TypeStyle::BodyStrong
                                                                                      : tokens::TypeStyle::Body;
    // Content (icon + gap + text) centered as a group.
    const float textWidth = std::max(m_textWidth, 0.0f);
    const float content = textWidth + (m_icon ? tokens::size::icon + kIconGap : 0.0f);
    float x = b.x + std::max(kPaddingX, std::round((b.width - content) / 2));
    if (m_icon) {
        canvas.drawIcon(*m_icon, {x, b.y + std::round((b.height - tokens::size::icon) / 2)}, ink);
        x += tokens::size::icon + kIconGap;
    }
    canvas.drawText(m_text, {x, b.y, b.right() - kPaddingX - x + 1, b.height}, style, ink);
}

} // namespace wl::ui
