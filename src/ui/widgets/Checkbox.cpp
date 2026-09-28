#include "ui/widgets/Checkbox.h"

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

} // namespace wl::ui
