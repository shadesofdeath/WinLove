#include "ui/widgets/TabBar.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
using tokens::TypeStyle;
// D-107 (Painter::Tab, Painter::Segmented).
constexpr float kTabGap = 4.0f;    // D-114: text tabs are pills, 12 padding, 4 apart (were 2 apart, no padding)
constexpr float kTabPad = 12.0f;
constexpr float kTextFallback = 60.0f; // a plausible tab width before the bar has a host
constexpr float kBadgeGap = 6.0f;
constexpr float kSegPad = 3.0f;      // segmented: outer padding, 2 between segments
constexpr float kSegItemPad = 12.0f; // text padding inside a segment
} // namespace

// ---- TabBar ---------------------------------------------------------------------------------

TabBar::TabBar(std::vector<std::wstring> tabs, int selected) : m_tabs(std::move(tabs)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::TabItem, m_tabs.empty() ? std::wstring() : m_tabs.front());
}

void TabBar::setSelected(int index) {
    m_selected = std::clamp(index, 0, static_cast<int>(m_tabs.size()) - 1);
    invalidate();
}

void TabBar::setBadges(std::vector<int> badges) {
    if (badges != m_badges) {
        m_badges = std::move(badges);
        invalidate();
    }
}

int TabBar::badge(std::size_t tab) const {
    return tab < m_badges.size() ? m_badges[tab] : 0;
}

SizeF TabBar::measure(SizeF available) {
    if (available.width > 0) {
        return {available.width, tokens::size::control};
    }
    // Unbounded (measure({})): the tabs' own width. Was 0, and a bar laid out with it took no clicks
    // (Özellikler: "İsteğe Bağlı Özellikler" could not be chosen, 2026-11-09).
    const auto rects = tabRects();
    return {rects.empty() ? 0.0f : rects.back().right() - bounds().x, tokens::size::control};
}

std::vector<RectF> TabBar::tabRects() const {
    std::vector<RectF> rects;
    const RectF b = bounds();
    float x = b.x;
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        const int n = badge(i);
        const float count = n > 0 ? kBadgeGap + textWidth(std::to_wstring(n), TypeStyle::Counter, 10.0f) : 0.0f;
        const float w = textWidth(m_tabs[i], TypeStyle::BodySemi, kTextFallback) + count + 2 + 2 * kTabPad;
        rects.push_back({x, b.y, w, b.height});
        x += w + kTabGap;
    }
    return rects;
}

void TabBar::paint(Canvas& canvas) {
    // Text tabs: the selected one BodySemi text.primary over a 2px accent underline, the others
    // BodyMedium text.secondary (text.primary on hover); a count after the title, accent text.
    const RectF b = bounds();
    canvas.hairlineH(b.x, b.bottom() - 1.0f / canvas.scale(), b.width, Color::LineSubtle);
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const bool selected = static_cast<int>(i) == m_selected;
        const bool hot = static_cast<int>(i) == m_hover;
        const RectF r = rects[i];
        const TypeStyle style = selected ? TypeStyle::BodySemi : TypeStyle::BodyStrong;
        const float tw = textWidth(m_tabs[i], style, kTextFallback);
        // D-114: a soft pill behind the chosen / hovered tab, a short accent bar under the chosen one.
        const RectF pill{r.x, r.y + 4, r.width, r.height - 8};
        if (selected) {
            canvas.fillRoundRect(pill, tokens::radius::r3, Color::BgRaised);
        } else if (hot) {
            canvas.fillRoundRect(pill, tokens::radius::r3, Color::BgHover);
        }
        const float tx = r.x + kTabPad;
        canvas.drawText(m_tabs[i], {tx, r.y, tw + 1, r.height}, style, selected || hot ? Color::TextPrimary : Color::TextSecondary);
        if (const int n = badge(i); n > 0) {
            const std::wstring count = std::to_wstring(n);
            const float x = tx + tw + kBadgeGap;
            canvas.drawText(count, {x, r.y, r.right() - x + 2, r.height}, TypeStyle::Counter, Color::AccentText);
        }
        if (selected) {
            const float bar = std::min(20.0f, r.width - 2 * kTabPad);
            canvas.fillRoundRect({r.x + (r.width - bar) / 2, r.bottom() - 3, bar, 3}, 1.5f, Color::AccentBase);
        }
    }
}

void TabBar::onPointerMove(PointF p) {
    int hover = -1;
    const auto rects = tabRects();
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

void TabBar::onHoverChanged(bool hovered) {
    if (!hovered && m_hover >= 0) {
        m_hover = -1; // no pointer move arrives after the pointer has left the bar
    }
    invalidate();
}

void TabBar::onPointerDown(PointF p) {
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p) && static_cast<int>(i) != m_selected) {
            m_selected = static_cast<int>(i);
            invalidate();
            if (onChange) {
                onChange(m_selected);
            }
        }
    }
}

bool TabBar::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey != VK_LEFT && key.virtualKey != VK_RIGHT) {
        return false;
    }
    const int next = std::clamp(m_selected + (key.virtualKey == VK_RIGHT ? 1 : -1), 0, static_cast<int>(m_tabs.size()) - 1);
    if (next != m_selected) {
        m_selected = next;
        invalidate();
        if (onChange) {
            onChange(next);
        }
    }
    return true;
}

// ---- RadioGroup -------------------------------------------------------------------------------

RadioGroup::RadioGroup(std::vector<std::wstring> options, int selected)
    : m_options(std::move(options)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::Group, L"");
}

void RadioGroup::setSelected(int index) {
    m_selected = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    invalidate();
}

std::vector<RectF> RadioGroup::optionRects() const {
    std::vector<RectF> rects;
    const RectF b = bounds();
    float x = b.x + kSegPad;
    for (const auto& option : m_options) {
        const float w = textWidth(option, TypeStyle::SmallSemi, kTextFallback) + 2 * kSegItemPad;
        rects.push_back({x, b.y, w, b.height});
        x += w + 2;
    }
    return rects;
}

SizeF RadioGroup::measure(SizeF /*available*/) {
    const auto rects = optionRects();
    return {rects.empty() ? 0.0f : rects.back().right() + kSegPad - bounds().x, tokens::size::segment};
}

void RadioGroup::onPointerMove(PointF p) {
    int hover = -1;
    const auto rects = optionRects();
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

void RadioGroup::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hover = -1;
    }
    invalidate();
}

void RadioGroup::paint(Canvas& canvas) {
    // D-107 (Painter::Segmented): a sunk 34px track (radius 9), the chosen segment raised on the
    // panel color with a strong line (radius 7) and SmallSemi text.
    const auto rects = optionRects();
    if (rects.empty()) {
        return;
    }
    const RectF b = bounds();
    const float height = std::min(b.height, tokens::size::segment);
    const RectF track{b.x, b.y + (b.height - height) / 2, rects.back().right() + kSegPad - b.x, height};
    // 2026-11-09 (the user: "more elegant, thinner"): a hairline around the track, the chosen
    // segment lifted on the panel color with a hairline of its own (was a strong 1 DIP line), a
    // soft hover fill on the others.
    canvas.fillRoundRect(track, tokens::radius::r2, Color::BgRaised);
    canvas.strokeRoundRect(track, tokens::radius::r2, Color::LineSubtle, 1.0f);
    for (std::size_t i = 0; i < rects.size(); ++i) {
        RectF r = rects[i];
        r.y = track.y + kSegPad;
        r.height = track.height - 2 * kSegPad;
        const bool selected = static_cast<int>(i) == m_selected;
        if (selected) {
            canvas.fillRoundRect(r, tokens::radius::menu, Color::BgPanel);
            canvas.strokeRoundRect(r, tokens::radius::menu, Color::LineSubtle, 1.0f);
        } else if (static_cast<int>(i) == m_hover) {
            canvas.fillRoundRect(r, tokens::radius::menu, Color::BgHover);
        }
        canvas.drawText(m_options[i], r, selected ? TypeStyle::SmallSemi : TypeStyle::Small,
                        selected || static_cast<int>(i) == m_hover ? Color::TextPrimary : Color::TextSecondary,
                        TextAlign::Center);
    }
}

void RadioGroup::choose(int index) {
    if (index < 0 || index >= static_cast<int>(m_options.size()) || index == m_selected) {
        return;
    }
    m_selected = index;
    invalidate();
    if (onChange) {
        onChange(index);
    }
}

void RadioGroup::onPointerDown(PointF p) {
    const auto rects = optionRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            choose(static_cast<int>(i));
        }
    }
}

bool RadioGroup::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_LEFT || key.virtualKey == VK_RIGHT) {
        choose(m_selected + (key.virtualKey == VK_RIGHT ? 1 : -1));
        return true;
    }
    return false;
}

} // namespace wl::ui
