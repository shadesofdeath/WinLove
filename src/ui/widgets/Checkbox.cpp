#include "ui/widgets/Checkbox.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
PointF along(PointF a, PointF b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}
} // namespace

void Checkbox::paintBox(Canvas& canvas, PointF at, CheckState state, bool hovered, float reveal, bool dim) {
    // 2026-11-09 (the user: "everywhere the round style of the Components page"): the reference tool's tree
    // check (app/pages/PageBits.h paintTreeCheck) at form size — a 14 px circle in the 18 px box,
    // a tick / dash inside; no fill. Hover strengthens the ring. D-137: a ticked box is the accent's
    // (ring and tick in the readable accent tone, as every "on" state); the Components / Features
    // tree check (kept / removed, PageBits.h) stays neutral.
    using tokens::Color;
    constexpr float r = 7.0f;
    const PointF c{at.x + kBox / 2, at.y + kBox / 2};
    const RectF ring{c.x - r, c.y - r, 2 * r, 2 * r};
    hovered = hovered && !dim;
    canvas.fillRoundRect(ring, r, dim ? Color::BgPanel : Color::BgInput);
    const bool on = state != CheckState::Off;
    canvas.strokeRoundRect(ring, r, dim ? Color::LineSubtle : on ? Color::AccentText : hovered ? Color::TextTertiary : Color::LineStrong,
                           1.25f * canvas.scale());
    const float k = r / 6.0f; // paintTreeCheck's tick is drawn for a radius of 6
    const float w = std::max(1.0f, 1.5f * canvas.scale());
    const Ink ink = dim ? Color::TextTertiary : on ? Color::AccentText : hovered ? Color::TextPrimary : Color::TextSecondary;
    if (state == CheckState::On) {
        const PointF start{c.x - 3.0f * k, c.y + 0.2f * k};
        const PointF corner{c.x - 0.8f * k, c.y + 2.4f * k};
        const PointF end{c.x + 3.2f * k, c.y - 2.3f * k};
        if (reveal >= 1.0f) {
            canvas.line(start, corner, ink, w);
            canvas.line(corner, end, ink, w);
        } else if (reveal > 0.0f) {
            // Drawn in: the short stroke down, then the long one up, at one pace.
            const float first = std::hypot(corner.x - start.x, corner.y - start.y);
            const float second = std::hypot(end.x - corner.x, end.y - corner.y);
            const float drawn = reveal * (first + second);
            canvas.line(start, along(start, corner, std::min(drawn / first, 1.0f)), ink, w);
            if (drawn > first) {
                canvas.line(corner, along(corner, end, (drawn - first) / second), ink, w);
            }
        }
    } else if (state == CheckState::Indeterminate) {
        if (reveal >= 1.0f) {
            canvas.line({c.x - 2.8f * k, c.y}, {c.x + 2.8f * k, c.y}, ink, w);
        } else if (reveal > 0.0f) {
            const float half = 2.8f * k * reveal; // grows out from the middle
            canvas.line({c.x - half, c.y}, {c.x + half, c.y}, ink, w);
        }
    }
}

CheckField::CheckField(std::wstring label, bool checked) : m_label(std::move(label)), m_checked(checked) {
    setFocusable(true);
    setAccessible(AccessRole::CheckBox, m_label);
}

void CheckField::setChecked(bool checked) {
    if (checked != m_checked) {
        m_reveal.snapTo(1.0f); // changed by code: no drawing in
    }
    // The same value again (a page syncing from its state inside onChange) lets a tick go on.
    m_checked = checked;
    invalidate();
}

SizeF CheckField::measure(SizeF /*available*/) {
    const float label = textWidth(m_label, tokens::TypeStyle::Body);
    return {Checkbox::kBox + 10 + label, tokens::size::control};
}

void CheckField::setLabel(std::wstring label) {
    m_label = std::move(label);
    setAccessible(AccessRole::CheckBox, m_label);
    invalidate();
}

void CheckField::paint(Canvas& canvas) {
    const RectF b = bounds();
    Checkbox::paintBox(canvas, {b.x, b.y + (b.height - Checkbox::kBox) / 2}, m_checked ? CheckState::On : CheckState::Off,
                       hovered(), m_reveal.value());
    const float x = b.x + Checkbox::kBox + 10;
    canvas.drawText(m_label, {x, b.y, b.right() - x, b.height}, tokens::TypeStyle::Body, tokens::Color::TextPrimary);
}

void CheckField::onClick() {
    m_checked = !m_checked;
    if (m_checked) {
        m_reveal.snapTo(0.0f);
        if (m_reveal.animateTo(1.0f, Checkbox::kRevealMs)) {
            animate();
        }
    } else {
        m_reveal.snapTo(1.0f);
    }
    invalidate();
    if (onChange) {
        onChange(m_checked);
    }
}

bool CheckField::onKeyDown(const KeyEvent& key) {
    return activateOnKey(key, /*enterToo=*/false);
}

bool CheckField::tick(double now) {
    const bool running = m_reveal.tick(now);
    invalidate();
    return running;
}

} // namespace wl::ui
