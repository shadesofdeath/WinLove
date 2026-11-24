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

// The plain colour at either end, so a frame at rest is drawn exactly as before the fades existed.
Ink blend(Color from, Color to, float t) {
    return t >= 1.0f ? Ink(to) : t <= 0.0f ? Ink(from) : Ink(from, to, t);
}

} // namespace

PillGroup::PillGroup(std::vector<std::wstring> options, int selected) : m_options(std::move(options)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::Group, m_options.empty() ? std::wstring() : m_options[static_cast<std::size_t>(selected)]);
}

void PillGroup::setSelected(int index) {
    const int selected = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    if (selected != m_selected) {
        m_move.stop(); // as TabBar::setSelected
    }
    m_selected = selected;
    invalidate();
}

void PillGroup::setOptionEnabled(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(m_options.size())) {
        return;
    }
    m_disabled.resize(m_options.size(), false);
    if (m_disabled[static_cast<std::size_t>(index)] != !enabled) {
        m_disabled[static_cast<std::size_t>(index)] = !enabled;
        invalidate();
    }
}

bool PillGroup::optionEnabled(int index) const noexcept {
    return index < 0 || static_cast<std::size_t>(index) >= m_disabled.size() || !m_disabled[static_cast<std::size_t>(index)];
}

std::vector<RectF> PillGroup::pillRects() const {
    std::vector<RectF> rects;
    pillRects(rects);
    return rects;
}

void PillGroup::pillRects(std::vector<RectF>& rects) const {
    rects.clear();
    const RectF b = bounds();
    const float h = tokens::size::pill;
    float x = b.x;
    for (const auto& option : m_options) {
        const float w = std::ceil(textWidth(option, TypeStyle::SmallSemi, kTextFallback)) + 2 * kPadX;
        rects.push_back({x, b.y + std::round((b.height - h) / 2), w, h});
        x += w + kGap;
    }
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
    pillRects(m_rects);
    // A new choice: the chosen pill's fill fades in while the old one's fades out.
    const int from = m_move.from();
    const float t = m_move.progress();
    for (std::size_t i = 0; i < m_rects.size(); ++i) {
        const RectF r = m_rects[i];
        const float radius = r.height / 2;
        const bool selected = static_cast<int>(i) == m_selected;
        const float fill = selected ? (from >= 0 ? t : 1.0f) : static_cast<int>(i) == from ? 1.0f - t : 0.0f;
        if (fill >= 1.0f) {
            canvas.fillRoundRect(r, radius, Color::TextPrimary);
            canvas.drawText(m_options[i], r, TypeStyle::SmallSemi, Color::BgBase, TextAlign::Center);
            continue;
        }
        if (!optionEnabled(static_cast<int>(i)) && fill <= 0) {
            canvas.strokeRoundRect(r, radius, Color::LineSubtle);
            canvas.drawText(m_options[i], r, TypeStyle::Small, Color::TextDisabled, TextAlign::Center);
            continue;
        }
        const float hot = m_hoverFade.amount(static_cast<int>(i));
        canvas.strokeRoundRect(r, radius, blend(Color::LineSubtle, Color::LineStrong, hot));
        if (fill > 0) {
            canvas.fillRoundRect(r, radius, Ink(Color::TextPrimary, Color::TextPrimary, 0.0f, fill));
        }
        canvas.drawText(m_options[i], r, selected ? TypeStyle::SmallSemi : TypeStyle::Small,
                        fill > 0 ? blend(Color::TextSecondary, Color::BgBase, fill) : blend(Color::TextSecondary, Color::TextPrimary, hot),
                        TextAlign::Center);
    }
}

void PillGroup::hoverTo(int index) {
    if (index == m_hover) {
        return;
    }
    m_hover = index;
    if (m_hoverFade.set(index)) {
        animate();
    }
    invalidate();
}

void PillGroup::onPointerMove(PointF p) {
    int hover = -1;
    const auto rects = pillRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    hoverTo(hover);
}

void PillGroup::onHoverChanged(bool hovered) {
    if (!hovered) {
        hoverTo(-1);
    }
    invalidate();
}

void PillGroup::choose(int index) {
    if (index < 0 || index >= static_cast<int>(m_options.size()) || index == m_selected || !optionEnabled(index)) {
        return;
    }
    if (m_move.start(m_selected, {}, {})) { // a fade, not a slide: no rectangle
        animate();
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
            return;
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

bool PillGroup::tick(double now) {
    const bool hover = m_hoverFade.tick(now);
    const bool move = m_move.tick(now);
    invalidate();
    return hover || move;
}

} // namespace wl::ui
