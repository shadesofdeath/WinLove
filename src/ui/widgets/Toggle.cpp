#include "ui/widgets/Toggle.h"

#include "ui/widget/Host.h"

#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
constexpr float kGap = 10.0f;
constexpr float kKnob = tokens::size::toggleThumb;
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
    const float label = m_label.empty() ? 0.0f : textWidth(m_label, tokens::TypeStyle::Body);
    return {tokens::size::toggleW + (label > 0 ? kGap + label : 0), tokens::size::control};
}

RectF Toggle::focusRect() const {
    const RectF b = bounds();
    return {b.x, b.y + (b.height - tokens::size::toggleH) / 2, tokens::size::toggleW, tokens::size::toggleH};
}

void Toggle::paintSwitch(Canvas& canvas, RectF track, float t, bool hovered) {
    // D-107 (Painter::Switch): a pill that blends line.strong (off) into the accent (on), a white
    // knob with a soft shadow under it.
    const float radius = track.height / 2;
    canvas.fillRoundRect(track, radius, Ink(hovered ? Color::TextTertiary : Color::LineStrong, Color::AccentBase, t));
    const float pad = (track.height - kKnob) / 2;
    const float cx = track.x + pad + kKnob / 2 + t * (track.width - 2 * pad - kKnob);
    const float cy = track.y + track.height / 2;
    canvas.fillEllipse({cx, cy + 1}, kKnob / 2, Ink(Color::ShadowKey, Color::ShadowKey, 0, 0.6f));
    canvas.fillEllipse({cx, cy}, kKnob / 2, Color::TextOnAccent);
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
    return activateOnKey(key, /*enterToo=*/false);
}

bool Toggle::tick(double now) {
    const bool running = m_knob.tick(now);
    invalidate();
    return running;
}

} // namespace wl::ui
