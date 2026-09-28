#include "app/shell/StatusBar.h"

#include "ui/widget/Host.h"

#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kPaddingLeft = 12.0f;
constexpr float kPaddingRight = 4.0f;
constexpr float kCtaHeight = 20.0f;
constexpr float kCtaPaddingX = 10.0f;
constexpr float kDot = 6.0f;
constexpr float kDotGap = 6.0f;
} // namespace

// ---- ApplyCta ----------------------------------------------------------------------------------

ApplyCta::ApplyCta(std::wstring label) : m_label(std::move(label)) {
    setFocusable(true);
    setAccessible(ui::AccessRole::Button, m_label);
    setQueue(0);
}

std::wstring ApplyCta::text() const {
    return m_queue > 0 ? m_label + L" · " + std::to_wstring(m_queue) : m_label;
}

void ApplyCta::setQueue(int count) {
    m_queue = count;
    // Stays focusable/hoverable-free while empty: nothing to apply.
    setFocusable(count > 0);
    setHitTestVisible(count > 0);
    invalidate();
}

ui::SizeF ApplyCta::measure(ui::SizeF /*available*/) {
    const float textWidth = host() ? std::ceil(host()->text().measure(text(), TypeStyle::BodyStrong)) : 0.0f;
    return {textWidth + 2 * kCtaPaddingX, kCtaHeight};
}

void ApplyCta::onHoverChanged(bool hovered) {
    if (m_hover.animateTo(hovered ? 1.0f : 0.0f, ui::tokens::motion::fastMs)) {
        animate();
    }
    invalidate();
}

bool ApplyCta::tick(double now) {
    const bool running = m_hover.tick(now);
    invalidate();
    return running;
}

void ApplyCta::onClick() {
    if (m_queue > 0 && onInvoke) {
        onInvoke();
    }
}

bool ApplyCta::onKeyDown(const ui::KeyEvent& key) {
    if (key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN) {
        onClick();
        return true;
    }
    return false;
}

void ApplyCta::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float radius = ui::tokens::radius::r2;
    Color ink = Color::TextOnAccent;
    if (m_queue == 0) {
        canvas.fillRoundRect(b, radius, Color::BgRaised);
        ink = Color::TextDisabled;
    } else if (pressed()) {
        canvas.fillRoundRect(b, radius, Color::AccentPressed);
    } else {
        const float h = hovered() && !m_hover.running() ? 1.0f : m_hover.value();
        canvas.fillRoundRect(b, radius, ui::Ink(Color::AccentBase, Color::AccentHover, h));
    }
    canvas.drawText(text(), b, TypeStyle::BodyStrong, ink, ui::TextAlign::Center);
}

// ---- StatusBar ---------------------------------------------------------------------------------

StatusBar::StatusBar(std::wstring noMountLabel, std::wstring applyLabel) : m_noMount(std::move(noMountLabel)) {
    m_cta = &add<ApplyCta>(std::move(applyLabel));
    // screens.md 01: no CTA until an image is mounted (Faz 2/P02 turns it on).
    m_cta->setVisible(false);
    setAccessible(ui::AccessRole::Group, L"Status");
}

void StatusBar::layout() {
    const RectF b = bounds();
    const ui::SizeF cta = m_cta->measure({});
    m_cta->setBounds({b.right() - kPaddingRight - cta.width, b.y + std::round((b.height - kCtaHeight) / 2), cta.width,
                      kCtaHeight});
}

void StatusBar::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineH(b.x, b.y, b.width, Color::LineSubtle);
    // Mount segment: 6px dot (tertiary = nothing mounted) + label.
    const float dotY = b.y + std::round((b.height - kDot) / 2);
    canvas.fillRoundRect({b.x + kPaddingLeft, dotY, kDot, kDot}, 1, Color::TextTertiary);
    const float labelX = b.x + kPaddingLeft + kDot + kDotGap;
    canvas.drawText(m_noMount, {labelX, b.y, m_cta->bounds().x - labelX - kPaddingLeft, b.height}, TypeStyle::Caption,
                    Color::TextSecondary);
}

} // namespace wl::app
