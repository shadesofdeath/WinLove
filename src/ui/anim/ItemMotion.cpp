#include "ui/anim/ItemMotion.h"

namespace wl::ui {

bool ItemHover::set(int index) noexcept {
    if (index == m_current) {
        return running();
    }
    // Back onto the item still fading out: it goes on from where it is, not from nothing.
    const float leaving = m_in.value();
    const float coming = index >= 0 && index == m_previous ? m_out.value() : 0.0f;
    m_previous = m_current;
    m_out.snapTo(leaving);
    m_out.animateTo(0.0f, tokens::motion::fastMs);
    m_current = index;
    m_in.snapTo(coming);
    if (index >= 0) {
        m_in.animateTo(1.0f, tokens::motion::fastMs);
    }
    return running();
}

void ItemHover::snap(int index) noexcept {
    m_current = index;
    m_previous = -1;
    m_in.snapTo(index >= 0 ? 1.0f : 0.0f);
    m_out.snapTo(0.0f);
}

bool ItemHover::tick(double now) noexcept {
    const bool in = m_in.tick(now);
    const bool out = m_out.tick(now);
    return in || out;
}

float ItemHover::amount(int index) const noexcept {
    if (index < 0) {
        return 0.0f;
    }
    if (index == m_current) {
        return m_in.value();
    }
    return index == m_previous ? m_out.value() : 0.0f;
}

bool SelectionMove::start(int from, RectF rect, PointF origin, float durationMs) noexcept {
    m_from = from;
    m_fromRect = {rect.x - origin.x, rect.y - origin.y, rect.width, rect.height};
    m_progress.snapTo(0.0f);
    return m_progress.animateTo(1.0f, durationMs);
}

void SelectionMove::stop() noexcept {
    m_progress.snapTo(1.0f);
    m_from = -1;
}

RectF SelectionMove::at(RectF to, PointF origin) const noexcept {
    if (!moving()) {
        return to;
    }
    const float t = m_progress.value();
    const float x = m_fromRect.x + origin.x;
    const float y = m_fromRect.y + origin.y;
    auto mix = [t](float a, float b) { return a + (b - a) * t; };
    return {mix(x, to.x), mix(y, to.y), mix(m_fromRect.width, to.width), mix(m_fromRect.height, to.height)};
}

} // namespace wl::ui
