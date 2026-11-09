#include "ui/widgets/Checkbox.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

void Checkbox::paintBox(Canvas& canvas, PointF at, CheckState state, bool hovered) {
    // 2026-11-09 (the user: "everywhere the round style of the Components page"): the reference tool's tree
    // check (app/pages/PageBits.h paintTreeCheck) at form size — a 14 px circle in the 18 px box,
    // a tick / dash inside; no fill, the tick in the text color. Hover strengthens the ring.
    using tokens::Color;
    constexpr float r = 7.0f;
    const PointF c{at.x + kBox / 2, at.y + kBox / 2};
    const RectF ring{c.x - r, c.y - r, 2 * r, 2 * r};
    canvas.fillRoundRect(ring, r, Color::BgInput);
    const bool on = state != CheckState::Off;
    canvas.strokeRoundRect(ring, r, hovered || on ? Color::TextTertiary : Color::LineStrong, 1.25f * canvas.scale());
    const float k = r / 6.0f; // paintTreeCheck's tick is drawn for a radius of 6
    const float w = std::max(1.0f, 1.5f * canvas.scale());
    const Ink ink = hovered ? Color::TextPrimary : Color::TextSecondary;
    if (state == CheckState::On) {
        canvas.line({c.x - 3.0f * k, c.y + 0.2f * k}, {c.x - 0.8f * k, c.y + 2.4f * k}, ink, w);
        canvas.line({c.x - 0.8f * k, c.y + 2.4f * k}, {c.x + 3.2f * k, c.y - 2.3f * k}, ink, w);
    } else if (state == CheckState::Indeterminate) {
        canvas.line({c.x - 2.8f * k, c.y}, {c.x + 2.8f * k, c.y}, ink, w);
    }
}

CheckField::CheckField(std::wstring label, bool checked) : m_label(std::move(label)), m_checked(checked) {
    setFocusable(true);
    setAccessible(AccessRole::CheckBox, m_label);
}

void CheckField::setChecked(bool checked) {
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
                       hovered());
    const float x = b.x + Checkbox::kBox + 10;
    canvas.drawText(m_label, {x, b.y, b.right() - x, b.height}, tokens::TypeStyle::Body, tokens::Color::TextPrimary);
}

void CheckField::onClick() {
    m_checked = !m_checked;
    invalidate();
    if (onChange) {
        onChange(m_checked);
    }
}

bool CheckField::onKeyDown(const KeyEvent& key) {
    return activateOnKey(key, /*enterToo=*/false);
}

} // namespace wl::ui
