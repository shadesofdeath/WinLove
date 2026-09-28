#include "app/shell/StatusBar.h"

#include "ui/widget/Host.h"

#include <algorithm>
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
constexpr float kSegmentGap = 12.0f;
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

StatusBar::StatusBar(Labels labels) : m_labels(std::move(labels)) {
    m_cta = &add<ApplyCta>(m_labels.apply);
    // screens.md 01: no CTA until an image is mounted.
    m_cta->setVisible(false);
    setAccessible(ui::AccessRole::Group, L"Status");
}

void StatusBar::setMount(std::optional<std::wstring> mountPath, std::wstring size) {
    m_mountPath = std::move(mountPath);
    m_size = std::move(size);
    m_cta->setVisible(m_mountPath.has_value());
    layout();
    invalidate();
}

void StatusBar::setTask(std::optional<std::wstring> label, float fraction) {
    m_task = std::move(label);
    m_taskFraction = fraction;
    invalidate();
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
    const float right = (m_cta->visible() ? m_cta->bounds().x : b.right()) - kPaddingLeft;
    auto text = [&](float& x, std::wstring_view value, TypeStyle style, Color ink) {
        const float w = std::ceil(canvas.text().measure(value, style)) + 1;
        canvas.drawText(value, {x, b.y, std::max(std::min(w, right - x), 0.0f), b.height}, style, ink);
        x += w;
    };
    auto separator = [&](float& x) {
        x += kSegmentGap;
        canvas.hairlineV(x, b.y + (b.height - 12) / 2, 12, Color::LineStrong);
        x += kSegmentGap;
    };

    // Mount segment: 6px dot (success = mounted, tertiary = nothing) + label (+ mono path).
    const float dotY = b.y + std::round((b.height - kDot) / 2);
    canvas.fillRoundRect({b.x + kPaddingLeft, dotY, kDot, kDot}, 1, m_mountPath ? Color::StatusSuccess : Color::TextTertiary);
    float x = b.x + kPaddingLeft + kDot + kDotGap;
    if (m_mountPath) {
        text(x, m_labels.mounted, TypeStyle::Caption, Color::TextSecondary);
        x += kDotGap;
        text(x, *m_mountPath, TypeStyle::Mono, Color::TextTertiary);
        separator(x);
        text(x, m_labels.image, TypeStyle::Caption, Color::TextSecondary);
        x += kDotGap;
        text(x, m_size, TypeStyle::Mono, Color::TextPrimary);
    } else {
        text(x, m_labels.noMount, TypeStyle::Caption, Color::TextSecondary);
    }

    // Task segment (right-aligned before the CTA): label · 80×2 bar · percent.
    if (m_task) {
        constexpr float kBar = 80.0f;
        const std::wstring pct = std::to_wstring(static_cast<int>(m_taskFraction * 100)) + L"%";
        const float pctW = std::ceil(canvas.text().measure(pct, TypeStyle::Caption)) + 1;
        const float labelW = std::ceil(canvas.text().measure(*m_task, TypeStyle::Caption)) + 1;
        float tx = right - pctW - 8 - kBar - 8 - labelW;
        canvas.drawText(*m_task, {tx, b.y, labelW, b.height}, TypeStyle::Caption, Color::TextSecondary);
        tx += labelW + 8;
        canvas.progressBar({tx, b.y + b.height / 2 - 1, kBar, 2}, m_taskFraction, Color::TextSecondary);
        tx += kBar + 8;
        canvas.drawText(pct, {tx, b.y, pctW, b.height}, TypeStyle::Caption, Color::TextSecondary);
    }
}

} // namespace wl::app
