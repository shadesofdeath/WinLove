#include "ui/widget/Stack.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

SizeF Stack::measure(SizeF available) {
    float main = 0;
    float cross = 0;
    int visibleCount = 0;
    const auto kids = children();
    for (std::size_t i = 0; i < kids.size(); ++i) {
        if (!kids[i]->visible()) {
            continue;
        }
        const SizeF s = kids[i]->measure(available);
        const auto& sizing = m_items[i].sizing;
        main += sizing.mode == Sizing::Mode::Fixed ? sizing.value : mainOf(s);
        cross = std::max(cross, crossOf(s));
        ++visibleCount;
    }
    main += m_gap * static_cast<float>(std::max(visibleCount - 1, 0));
    const float padMain = m_axis == Axis::Horizontal ? m_padding.left + m_padding.right : m_padding.top + m_padding.bottom;
    const float padCross = m_axis == Axis::Horizontal ? m_padding.top + m_padding.bottom : m_padding.left + m_padding.right;
    return m_axis == Axis::Horizontal ? SizeF{main + padMain, cross + padCross} : SizeF{cross + padCross, main + padMain};
}

void Stack::layout() {
    const RectF b = bounds();
    const RectF inner{b.x + m_padding.left, b.y + m_padding.top, b.width - m_padding.left - m_padding.right,
                      b.height - m_padding.top - m_padding.bottom};
    const bool horizontal = m_axis == Axis::Horizontal;
    const float innerMain = horizontal ? inner.width : inner.height;
    const float innerCross = horizontal ? inner.height : inner.width;
    const auto kids = children();

    // Pass 1: fixed and auto sizes, total fill weight.
    std::vector<float> sizes(kids.size(), 0);
    std::vector<SizeF> measured(kids.size());
    float used = 0;
    float fillWeight = 0;
    int visibleCount = 0;
    for (std::size_t i = 0; i < kids.size(); ++i) {
        if (!kids[i]->visible()) {
            continue;
        }
        ++visibleCount;
        const auto& sizing = m_items[i].sizing;
        measured[i] = kids[i]->measure(horizontal ? SizeF{0, innerCross} : SizeF{innerCross, 0});
        if (sizing.mode == Sizing::Mode::Fixed) {
            sizes[i] = sizing.value;
        } else if (sizing.mode == Sizing::Mode::Auto) {
            sizes[i] = mainOf(measured[i]);
        } else {
            fillWeight += sizing.value;
            continue;
        }
        used += sizes[i];
    }
    used += m_gap * static_cast<float>(std::max(visibleCount - 1, 0));
    const float remaining = std::max(innerMain - used, 0.0f);

    // Pass 2: place.
    float cursor = horizontal ? inner.x : inner.y;
    for (std::size_t i = 0; i < kids.size(); ++i) {
        if (!kids[i]->visible()) {
            continue;
        }
        const auto& item = m_items[i];
        float size = sizes[i];
        if (item.sizing.mode == Sizing::Mode::Fill) {
            size = fillWeight > 0 ? std::floor(remaining * item.sizing.value / fillWeight) : 0;
        }
        float crossSize = innerCross;
        float crossPos = horizontal ? inner.y : inner.x;
        if (item.align != CrossAlign::Stretch) {
            crossSize = std::min(crossOf(measured[i]), innerCross);
            const float slack = innerCross - crossSize;
            crossPos += item.align == CrossAlign::Center ? std::round(slack / 2) : item.align == CrossAlign::End ? slack : 0;
        }
        kids[i]->setBounds(horizontal ? RectF{cursor, crossPos, size, crossSize} : RectF{crossPos, cursor, crossSize, size});
        cursor += size + m_gap;
    }
}

} // namespace wl::ui
