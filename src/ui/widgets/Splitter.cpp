#include "ui/widgets/Splitter.h"

#include <cmath>

namespace wl::ui {

Splitter::Splitter() {
    setAccessible(AccessRole::Group, L"Splitter");
}

void Splitter::paint(Canvas& canvas) {
    const RectF b = bounds();
    const auto color = m_dragging ? tokens::Color::AccentBase : hovered() ? tokens::Color::LineStrong : tokens::Color::LineSubtle;
    canvas.hairlineV(b.x + std::floor(b.width / 2), b.y, b.height, color);
}

void Splitter::onPointerDown(PointF p) {
    m_dragging = true;
    m_dragStartX = p.x;
    if (onDragStart) {
        onDragStart();
    }
    invalidate();
}

void Splitter::onPointerMove(PointF p) {
    if (m_dragging && onDrag) {
        onDrag(p.x - m_dragStartX);
    }
}

void Splitter::onPointerUp(PointF /*p*/) {
    m_dragging = false;
    invalidate();
}

void Splitter::onPressedChanged(bool pressed) {
    if (!pressed) {
        m_dragging = false; // also when the press is dropped without an Up (a modal opened)
    }
    invalidate();
}

void Splitter::onDoubleClick() {
    if (onReset) {
        onReset();
    }
}

} // namespace wl::ui
