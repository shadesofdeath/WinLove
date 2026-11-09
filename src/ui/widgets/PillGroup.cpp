#include "ui/widgets/PillGroup.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

using tokens::Color;
using tokens::TypeStyle;

constexpr float kGap = 6.0f;
constexpr float kPadX = 12.0f;
constexpr float kTextFallback = 56.0f;

} // namespace

PillGroup::PillGroup(std::vector<std::wstring> options, int selected) : m_options(std::move(options)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::Group, m_options.empty() ? std::wstring() : m_options[static_cast<std::size_t>(selected)]);
}

void PillGroup::setSelected(int index) {
    m_selected = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    invalidate();
}

std::vector<RectF> PillGroup::pillRects() const {
    std::vector<RectF> rects;
    const RectF b = bounds();
    const float h = tokens::size::pill;
    float x = b.x;
    for (const auto& option : m_options) {
        const float w = std::ceil(textWidth(option, TypeStyle::SmallSemi, kTextFallback)) + 2 * kPadX;
        rects.push_back({x, b.y + std::round((b.height - h) / 2), w, h});
        x += w + kGap;
    }
    return rects;
}

SizeF PillGroup::measure(SizeF /*available*/) {
    const auto rects = pillRects();
    return {rects.empty() ? 0.0f : rects.back().right() - bounds().x, tokens::size::control};
}

RectF PillGroup::focusRect() const {
    const auto rects = pillRects();
    return m_selected >= 0 && m_selected < static_cast<int>(rects.size()) ? rects[static_cast<std::size_t>(m_selected)] : bounds();
}

void PillGroup::paint(Canvas& canvas) {
    const auto rects = pillRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const RectF r = rects[i];
        const float radius = r.height / 2;
        if (static_cast<int>(i) == m_selected) {
            canvas.fillRoundRect(r, radius, Color::TextPrimary);
            canvas.drawText(m_options[i], r, TypeStyle::SmallSemi, Color::BgBase, TextAlign::Center);
        } else {
            const bool hot = static_cast<int>(i) == m_hover;
            canvas.strokeRoundRect(r, radius, hot ? Color::LineStrong : Color::LineSubtle);
            canvas.drawText(m_options[i], r, TypeStyle::Small, hot ? Color::TextPrimary : Color::TextSecondary, TextAlign::Center);
        }
    }
}

void PillGroup::onPointerMove(PointF p) {
    int hover = -1;
    const auto rects = pillRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    if (hover != m_hover) {
        m_hover = hover;
        invalidate();
    }
}

void PillGroup::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hover = -1;
    }
    invalidate();
}

void PillGroup::choose(int index) {
    if (index < 0 || index >= static_cast<int>(m_options.size()) || index == m_selected) {
        return;
    }
    m_selected = index;
    setAccessible(AccessRole::Group, m_options[static_cast<std::size_t>(index)]);
    invalidate();
    if (auto change = onChange) {
        change(index);
    }
}

void PillGroup::onPointerDown(PointF p) {
    const auto rects = pillRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            choose(static_cast<int>(i));
        }
    }
}

bool PillGroup::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_LEFT || key.virtualKey == VK_RIGHT) {
        choose(m_selected + (key.virtualKey == VK_RIGHT ? 1 : -1));
        return true;
    }
    return false;
}

} // namespace wl::ui
