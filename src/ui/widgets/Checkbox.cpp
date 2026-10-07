#include "ui/widgets/Checkbox.h"

#include "ui/widget/Host.h"

#include <cmath>

namespace wl::ui {

void Checkbox::paintBox(Canvas& canvas, PointF at, CheckState state, bool hovered) {
    using tokens::Color;
    const RectF box{at.x, at.y, kBox, kBox};
    const float r = tokens::radius::r1;
    switch (state) {
    case CheckState::Off:
        canvas.strokeRoundRect(box, r, hovered ? Color::TextTertiary : Color::LineStrong);
        break;
    case CheckState::On: {
        canvas.fillRoundRect(box, r, Color::AccentBase);
        // Tick (12 grid): M2.5 6 l2.5 2.5 4.5 -5, stroke 1.5 DIP, round caps.
        const float w = 1.5f * canvas.scale();
        canvas.line({at.x + 2.5f, at.y + 6.0f}, {at.x + 5.0f, at.y + 8.5f}, Color::TextOnAccent, w);
        canvas.line({at.x + 5.0f, at.y + 8.5f}, {at.x + 9.5f, at.y + 3.5f}, Color::TextOnAccent, w);
        break;
    }
    case CheckState::Indeterminate:
        canvas.strokeRoundRect(box, r, Color::AccentBase);
        canvas.fillRect({at.x + 3, at.y + 5, 6, 2}, Color::AccentBase);
        break;
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
    return {Checkbox::kBox + 8 + label, tokens::size::control};
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
    const float x = b.x + Checkbox::kBox + 8;
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
