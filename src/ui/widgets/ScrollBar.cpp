#include "ui/widgets/ScrollBar.h"

#include <algorithm>

namespace wl::ui {

namespace {
using tokens::Color;
constexpr float kMinThumb = 24.0f;
constexpr float kEdge = 2.0f;
constexpr float kThin = 4.0f;
constexpr float kThick = 8.0f;
} // namespace

ScrollBar::ScrollBar() {
    setAccessible(AccessRole::None, L"");
}

void ScrollBar::setRange(float content, float viewport) {
    m_content = content;
    m_viewport = viewport;
    m_offset = std::clamp(m_offset, 0.0f, maxOffset());
    invalidate();
}

void ScrollBar::setOffset(float offset) {
    m_offset = std::clamp(offset, 0.0f, maxOffset());
    invalidate();
}

float ScrollBar::maxOffset() const noexcept {
    return std::max(m_content - m_viewport, 0.0f);
}

RectF ScrollBar::thumbRect() const {
    const RectF b = bounds();
    const float track = b.height - 2 * kEdge;
    // std::clamp needs lo <= hi: a track shorter than the minimum thumb takes the whole track.
    const float length = std::clamp(track * m_viewport / std::max(m_content, 1.0f), std::min(kMinThumb, track), track);
    const float travel = track - length;
    const float y = b.y + kEdge + (maxOffset() > 0 ? travel * m_offset / maxOffset() : 0.0f);
    const float width = kThin + (kThick - kThin) * m_wide.value();
    return {b.right() - kEdge - width, y, width, length};
}

Widget* ScrollBar::hitTest(PointF p) {
    return needed() && visible() && bounds().contains(p) ? this : nullptr;
}

void ScrollBar::paint(Canvas& canvas) {
    if (!needed()) {
        return;
    }
    canvas.fillRoundRect(thumbRect(), tokens::radius::r1,
                         Ink(Color::LineStrong, Color::TextTertiary, m_wide.value()));
}

void ScrollBar::onHoverChanged(bool hovered) {
    if (m_wide.animateTo(hovered || captured() ? 1.0f : 0.0f, tokens::motion::baseMs)) {
        animate();
    }
    invalidate();
}

void ScrollBar::onPressedChanged(bool pressed) {
    if (!pressed) {
        m_grab = -1;
    }
    onHoverChanged(hovered());
}

void ScrollBar::scrollTo(float offset) {
    offset = std::clamp(offset, 0.0f, maxOffset());
    if (offset != m_offset) {
        m_offset = offset;
        invalidate();
        if (onScroll) {
            onScroll(m_offset);
        }
    }
}

void ScrollBar::onPointerDown(PointF p) {
    const RectF thumb = thumbRect();
    if (p.y >= thumb.y && p.y < thumb.bottom()) {
        m_grab = p.y - thumb.y;
        return;
    }
    // Track click: page towards the pointer, then drag from the middle of the thumb.
    scrollTo(m_offset + (p.y < thumb.y ? -m_viewport : m_viewport));
    m_grab = thumbRect().height / 2;
}

void ScrollBar::onPointerMove(PointF p) {
    if (m_grab < 0) {
        return;
    }
    const RectF b = bounds();
    const RectF thumb = thumbRect();
    const float travel = b.height - 2 * kEdge - thumb.height;
    if (travel <= 0) {
        return;
    }
    const float y = std::clamp(p.y - m_grab - b.y - kEdge, 0.0f, travel);
    scrollTo(maxOffset() * y / travel);
}

bool ScrollBar::tick(double now) {
    const bool running = m_wide.tick(now);
    invalidate();
    return running;
}

} // namespace wl::ui
