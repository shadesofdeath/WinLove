#include "ui/widgets/StripScroll.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

void StripScroll::setExtent(float content, float view) noexcept {
    m_content = std::max(content, 0.0f);
    m_view = std::max(view, 0.0f);
    m_offset = std::clamp(m_offset, 0.0f, maxOffset());
}

bool StripScroll::scrollTo(float offset) noexcept {
    const float next = std::round(std::clamp(offset, 0.0f, maxOffset()));
    if (next == m_offset) {
        return false;
    }
    m_offset = next;
    return true;
}

bool StripScroll::scrollBy(float delta) noexcept {
    return scrollTo(m_offset + delta);
}

bool StripScroll::reveal(float x, float width) noexcept {
    if (!overflows()) {
        return scrollTo(0);
    }
    const float end = x + width;
    // The edges that will still hide something after the move keep their chevron and fade over the
    // tabs: the item must clear them. The row's own first / last item needs no room (no chevron there).
    const float before = x > 0.5f ? kEdge : 0.0f;
    const float after = end < m_content - 0.5f ? kEdge : 0.0f;
    const float leftLimit = m_offset + (hidesStart() ? kEdge : 0.0f);
    const float rightLimit = m_offset + m_view - (hidesEnd() ? kEdge : 0.0f);
    if (x < leftLimit || width + before + after >= m_view) {
        return scrollTo(x - before);
    }
    if (end > rightLimit) {
        return scrollTo(end + after - m_view);
    }
    return false;
}

int StripScroll::chevronAt(float x) const noexcept {
    if (hidesStart() && x >= 0 && x < kChevron) {
        return -1;
    }
    if (hidesEnd() && x < m_view && x >= m_view - kChevron) {
        return 1;
    }
    return 0;
}

bool StripScroll::page(int direction) noexcept {
    const float step = std::max(m_view - 2 * kEdge, kEdge) * 0.8f;
    return scrollBy(direction < 0 ? -step : direction > 0 ? step : 0.0f);
}

void StripScroll::paintEdges(Canvas& canvas, RectF bounds, tokens::Color surface, int hotChevron) const {
    if (!overflows()) {
        return;
    }
    const float scale = canvas.scale();
    const float px = 1.0f / scale;
    const int steps = std::max(static_cast<int>(std::round(kFade * scale)), 1);
    auto edge = [&](bool start) {
        // The chevron's square: the surface, the chevron in the middle.
        const float squareX = start ? bounds.x : bounds.right() - kChevron;
        canvas.fillRect(snapRect({squareX, bounds.y, kChevron, bounds.height}, scale), surface);
        const Ink ink = hotChevron == (start ? -1 : 1) ? Ink(tokens::Color::TextPrimary) : Ink(tokens::Color::TextSecondary);
        canvas.drawIcon(start ? icons::Icon::ChevronLeft : icons::Icon::ChevronRight,
                        {squareX + std::round((kChevron - tokens::size::icon) / 2), bounds.y + std::round((bounds.height - tokens::size::icon) / 2)},
                        ink);
        // The fade: one physical pixel per step, the surface thinning out away from the chevron.
        const float fadeX = start ? snap(bounds.x + kChevron, scale) : snap(bounds.right() - kChevron, scale) - steps * px;
        for (int i = 0; i < steps; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps); // 0 at the fade's left end
            const float opacity = start ? 1.0f - t : t;
            // Each strip exactly one pixel, its edges on the grid: strips that overlapped by a sliver
            // (float steps of 1/scale) showed as light seams.
            const float x0 = snap(fadeX + static_cast<float>(i) * px, scale);
            const float x1 = snap(fadeX + static_cast<float>(i + 1) * px, scale);
            canvas.fillRect({x0, bounds.y, x1 - x0, bounds.height}, Ink(surface, surface, 0.0f, opacity));
        }
    };
    if (hidesStart()) {
        edge(true);
    }
    if (hidesEnd()) {
        edge(false);
    }
}

} // namespace wl::ui
