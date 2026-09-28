#include "ui/widgets/Toggle.h"

#include "ui/widget/Host.h"

#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
constexpr float kGap = 8.0f;
constexpr float kKnob = 8.0f;
} // namespace

Toggle::Toggle(std::wstring label, bool on) : m_label(std::move(label)), m_on(on), m_knob(on ? 1.0f : 0.0f) {
    setFocusable(true);
    setAccessible(AccessRole::CheckBox, m_label);
}

void Toggle::setOn(bool on, bool animated) {
    m_on = on;
    if (animated) {
        if (m_knob.animateTo(on ? 1.0f : 0.0f, tokens::motion::baseMs)) {
            animate();
        }
    } else {
        m_knob.snapTo(on ? 1.0f : 0.0f);
    }
    invalidate();
}

SizeF Toggle::measure(SizeF /*available*/) {
    const float label = host() && !m_label.empty() ? std::ceil(host()->text().measure(m_label, tokens::TypeStyle::Body)) : 0;
    return {tokens::size::toggleW + (label > 0 ? kGap + label : 0), tokens::size::control};
}

RectF Toggle::focusRect() const {
    const RectF b = bounds();
    return {b.x, b.y + (b.height - tokens::size::toggleH) / 2, tokens::size::toggleW, tokens::size::toggleH};
}

void Toggle::paintSwitch(Canvas& canvas, RectF track, float t, bool hovered) {
    // Track: outline (off) blends into accent fill (on).
    canvas.fillRoundRect(track, tokens::radius::r1, Ink(Color::BgInput, Color::AccentBase, t));
    if (t < 1.0f) {
        canvas.strokeRoundRect(track, tokens::radius::r1,
                               Ink(hovered ? Color::TextTertiary : Color::LineStrong, Color::AccentBase, t));
    }
    const float knobX = track.x + 2 + t * (tokens::size::toggleW - kKnob - 4);
    canvas.fillRoundRect({std::round(knobX), track.y + 2, kKnob, kKnob}, 1.0f,
                         Ink(Color::TextSecondary, Color::TextOnAccent, t));
}

void Toggle::paint(Canvas& canvas) {
    const RectF track = focusRect();
    paintSwitch(canvas, track, m_knob.value(), hovered());
    if (!m_label.empty()) {
        const RectF b = bounds();
        const float x = track.right() + kGap;
        canvas.drawText(m_label, {x, b.y, b.right() - x, b.height}, tokens::TypeStyle::Body, Color::TextPrimary);
    }
}

void Toggle::onClick() {
    setOn(!m_on);
    if (onChange) {
        onChange(m_on);
    }
}

bool Toggle::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_SPACE) {
        onClick();
        return true;
    }
    return false;
}

bool Toggle::tick(double now) {
    const bool running = m_knob.tick(now);
    invalidate();
    return running;
}

} // namespace wl::ui
