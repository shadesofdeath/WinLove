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

// A colour part of the way to another; the plain colour at either end, so a frame at rest is
// drawn exactly as before the fades existed.
Ink blend(Color from, Color to, float t) {
    return t >= 1.0f ? Ink(to) : t <= 0.0f ? Ink(from) : Ink(from, to, t);
}
Ink faded(Color color, float opacity) {
    return Ink(color, color, 0.0f, opacity);
}

// The selected tab's short accent bar.
RectF underline(RectF tab) {
    const float bar = std::min(20.0f, tab.width - 2 * kTabPad);
    return {tab.x + (tab.width - bar) / 2, tab.bottom() - 3, bar, 3};
}
} // namespace

// ---- TabBar ---------------------------------------------------------------------------------

TabBar::TabBar(std::vector<std::wstring> tabs, int selected) : m_tabs(std::move(tabs)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::TabItem, m_tabs.empty() ? std::wstring() : m_tabs.front());
}

void TabBar::setSelected(int index) {
    const int selected = std::clamp(index, 0, static_cast<int>(m_tabs.size()) - 1);
    if (selected != m_selected) {
        m_move.stop(); // another tab set by code: no slide from wherever the bar was
    }
    // The same one again (a page syncing from its state inside onChange) lets a slide go on.
    m_selected = selected;
    revealSelected();
    invalidate();
}

void TabBar::setBadges(std::vector<int> badges) {
    if (badges != m_badges) {
        m_badges = std::move(badges);
        updateExtent();
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
    return {contentWidth(), tokens::size::control};
}

std::vector<RectF> TabBar::tabRects() const {
    std::vector<RectF> rects;
    tabRects(rects);
    return rects;
}

void TabBar::tabRects(std::vector<RectF>& rects) const {
    rects.clear();
    const RectF b = bounds();
    float x = b.x - m_strip.offset();
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        const int n = badge(i);
        const float count = n > 0 ? kBadgeGap + textWidth(std::to_wstring(n), TypeStyle::Counter, 10.0f) : 0.0f;
        const float w = textWidth(m_tabs[i], TypeStyle::BodySemi, kTextFallback) + count + 2 + 2 * kTabPad;
        rects.push_back({x, b.y, w, b.height});
        x += w + kTabGap;
    }
}

RectF TabBar::tabRect(int index) const {
    const auto rects = tabRects();
    return index >= 0 && index < static_cast<int>(rects.size()) ? rects[static_cast<std::size_t>(index)] : RectF{};
}

float TabBar::contentWidth() const {
    const auto rects = tabRects();
    return rects.empty() ? 0.0f : rects.back().right() - rects.front().x;
}

void TabBar::updateExtent() {
    m_strip.setExtent(contentWidth(), bounds().width);
    revealSelected();
}

void TabBar::revealSelected() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_tabs.size()) || !m_strip.overflows()) {
        m_strip.scrollTo(0);
        return;
    }
    const auto rects = tabRects();
    const RectF r = rects[static_cast<std::size_t>(m_selected)];
    const float start = bounds().x - m_strip.offset(); // the row's start
    if (m_strip.reveal(r.x - start, r.width)) {
        invalidate();
    }
}

void TabBar::layout() {
    updateExtent(); // a bar laid out narrower (a smaller window) may hide the chosen tab
}

bool TabBar::onWheel(PointF p, float lines) {
    if (!m_strip.overflows()) {
        return false; // a bar that fits leaves the wheel to the page
    }
    if (m_strip.scrollBy(-lines * StripScroll::kWheelStep)) {
        invalidate();
    }
    onPointerMove(p); // no move follows a wheel: the tab under the pointer changed
    return true;
}

void TabBar::paint(Canvas& canvas) {
    // Text tabs: the selected one BodySemi text.primary over a 2px accent underline, the others
    // BodyMedium text.secondary (text.primary on hover); a count after the title, accent text.
    const RectF b = bounds();
    canvas.hairlineH(b.x, b.bottom() - 1.0f / canvas.scale(), b.width, Color::LineSubtle);
    tabRects(m_rects);
    if (const float content = m_rects.empty() ? 0.0f : m_rects.back().right() - m_rects.front().x;
        std::abs(content - m_strip.content()) > 0.5f || std::abs(b.width - m_strip.view()) > 0.5f) {
        // Laid out before the bar had a host (its tabs measured as fallbacks): once, the real extent.
        updateExtent();
        tabRects(m_rects);
    }
    const bool clipped = m_strip.overflows();
    if (clipped) {
        canvas.pushClip(b);
    }
    const auto& rects = m_rects;
    // A new tab was just chosen: its pill fades in over the old one's fading out, the underline
    // slides across (drawn last, above both).
    const bool moving = m_move.moving();
    const int from = m_move.from();
    const float t = m_move.progress();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const bool selected = static_cast<int>(i) == m_selected;
        const float hot = m_hoverFade.amount(static_cast<int>(i));
        const RectF r = rects[i];
        const TypeStyle style = selected ? TypeStyle::BodySemi : TypeStyle::BodyStrong;
        const float tw = textWidth(m_tabs[i], style, kTextFallback);
        // D-114: a soft pill behind the chosen / hovered tab, a short accent bar under the chosen one.
        const RectF pill{r.x, r.y + 4, r.width, r.height - 8};
        if (selected && !moving) {
            canvas.fillRoundRect(pill, tokens::radius::r3, Color::BgRaised);
        } else {
            if (hot > 0) {
                canvas.fillRoundRect(pill, tokens::radius::r3, faded(Color::BgHover, hot));
            }
            if (selected || static_cast<int>(i) == from) {
                canvas.fillRoundRect(pill, tokens::radius::r3, faded(Color::BgRaised, selected ? t : 1.0f - t));
            }
        }
        const float tx = r.x + kTabPad;
        canvas.drawText(m_tabs[i], {tx, r.y, tw + 1, r.height}, style,
                        selected ? Ink(Color::TextPrimary) : blend(Color::TextSecondary, Color::TextPrimary, hot));
        if (const int n = badge(i); n > 0) {
            const std::wstring count = std::to_wstring(n);
            const float x = tx + tw + kBadgeGap;
            canvas.drawText(count, {x, r.y, r.right() - x + 2, r.height}, TypeStyle::Counter, Color::AccentText);
        }
        if (selected && !moving) {
            canvas.fillRoundRect(underline(r), 1.5f, Color::AccentBase);
        }
    }
    // The row's origin: a slide started before a scroll keeps to the tabs it runs between.
    const PointF origin{b.x - m_strip.offset(), b.y};
    if (moving && m_selected >= 0 && m_selected < static_cast<int>(rects.size())) {
        canvas.fillRoundRect(m_move.at(underline(rects[static_cast<std::size_t>(m_selected)]), origin), 1.5f, Color::AccentBase);
    }
    if (clipped) {
        canvas.popClip();
        // Over the tabs at an edge that hides some: the fade and its chevron (the line under the bar stays).
        m_strip.paintEdges(canvas, {b.x, b.y, b.width, b.height - 1.0f / canvas.scale()}, m_surface, m_hotChevron);
    }
}

void TabBar::select(int index) {
    if (index == m_selected || index < 0 || index >= static_cast<int>(m_tabs.size())) {
        return;
    }
    tabRects(m_rects);
    if (m_selected >= 0 && m_selected < static_cast<int>(m_rects.size())) {
        // From where the bar is now: a choice made while it still slides goes on from there.
        const RectF b = bounds();
        const PointF origin{b.x - m_strip.offset(), b.y};
        const RectF now = m_move.at(underline(m_rects[static_cast<std::size_t>(m_selected)]), origin);
        if (m_move.start(m_selected, now, origin)) {
            animate();
        }
    }
    m_selected = index;
    revealSelected(); // the chosen tab whole in view (←/→ past the edge, a half-hidden tab clicked)
    invalidate();
    if (onChange) {
        onChange(index); // may rebuild the page around this bar: nothing after it
    }
}

void TabBar::hoverTo(int index) {
    if (index == m_hover) {
        return;
    }
    m_hover = index;
    if (m_hoverFade.set(index)) {
        animate();
    }
    invalidate();
}

void TabBar::onPointerMove(PointF p) {
    int hover = -1;
    const RectF b = bounds();
    const int chevron = b.contains(p) ? m_strip.chevronAt(p.x - b.x) : 0;
    if (chevron != m_hotChevron) {
        m_hotChevron = chevron;
        invalidate();
    }
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size() && chevron == 0 && b.contains(p); ++i) {
        if (rects[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    hoverTo(hover);
}

void TabBar::onHoverChanged(bool hovered) {
    if (!hovered) {
        hoverTo(-1); // no pointer move arrives after the pointer has left the bar
        m_hotChevron = 0;
    }
    invalidate();
}

void TabBar::onPointerDown(PointF p) {
    const RectF b = bounds();
    if (const int chevron = m_strip.chevronAt(p.x - b.x); chevron != 0) {
        if (m_strip.page(chevron)) { // a chevron scrolls; it chooses nothing
            invalidate();
        }
        onPointerMove(p);
        return;
    }
    const auto rects = tabRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p) && b.contains(p) && static_cast<int>(i) != m_selected) {
            select(static_cast<int>(i));
            return;
        }
    }
}

bool TabBar::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey != VK_LEFT && key.virtualKey != VK_RIGHT) {
        return false;
    }
    select(std::clamp(m_selected + (key.virtualKey == VK_RIGHT ? 1 : -1), 0, static_cast<int>(m_tabs.size()) - 1));
    return true;
}

bool TabBar::tick(double now) {
    const bool hover = m_hoverFade.tick(now);
    const bool move = m_move.tick(now);
    invalidate();
    return hover || move;
}

// ---- RadioGroup -------------------------------------------------------------------------------

RadioGroup::RadioGroup(std::vector<std::wstring> options, int selected)
    : m_options(std::move(options)), m_selected(selected) {
    setFocusable(true);
    setAccessible(AccessRole::Group, L"");
}

void RadioGroup::setSelected(int index) {
    const int selected = std::clamp(index, 0, static_cast<int>(m_options.size()) - 1);
    if (selected != m_selected) {
        m_move.stop(); // as TabBar::setSelected
    }
    m_selected = selected;
    invalidate();
}

std::vector<RectF> RadioGroup::optionRects() const {
    std::vector<RectF> rects;
    optionRects(rects);
    return rects;
}

void RadioGroup::optionRects(std::vector<RectF>& rects) const {
    rects.clear();
    const RectF b = bounds();
    float x = b.x + kSegPad;
    for (const auto& option : m_options) {
        const float w = textWidth(option, TypeStyle::SmallSemi, kTextFallback) + 2 * kSegItemPad;
        rects.push_back({x, b.y, w, b.height});
        x += w + 2;
    }
}

SizeF RadioGroup::measure(SizeF /*available*/) {
    const auto rects = optionRects();
    return {rects.empty() ? 0.0f : rects.back().right() + kSegPad - bounds().x, tokens::size::segment};
}

void RadioGroup::hoverTo(int index) {
    if (index == m_hover) {
        return;
    }
    m_hover = index;
    if (m_hoverFade.set(index)) {
        animate();
    }
    invalidate();
}

void RadioGroup::onPointerMove(PointF p) {
    int hover = -1;
    const auto rects = optionRects();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    hoverTo(hover);
}

void RadioGroup::onHoverChanged(bool hovered) {
    if (!hovered) {
        hoverTo(-1);
    }
    invalidate();
}

void RadioGroup::paint(Canvas& canvas) {
    // D-107 (Painter::Segmented): a sunk 34px track (radius 9), the chosen segment raised on the
    // panel color with a strong line (radius 7) and SmallSemi text.
    optionRects(m_rects);
    const auto& rects = m_rects;
    if (rects.empty()) {
        return;
    }
    const RectF b = bounds();
    const float height = std::min(b.height, tokens::size::segment);
    const RectF track{b.x, b.y + (b.height - height) / 2, rects.back().right() + kSegPad - b.x, height};
    auto segment = [&](std::size_t i) {
        RectF r = rects[i];
        r.y = track.y + kSegPad;
        r.height = track.height - 2 * kSegPad;
        return r;
    };
    // 2026-11-09 (the user: "more elegant, thinner"): a hairline around the track, the chosen
    // segment with a hairline of its own (was a strong 1 DIP line), a soft hover fill on the others.
    // D-137: the track is an input's well, the chosen segment the selection fill (was the panel
    // color: darker than the track in the dark theme, it read as pressed in).
    canvas.fillRoundRect(track, tokens::radius::r2, Color::BgInput);
    canvas.strokeRoundRect(track, tokens::radius::r2, Color::LineSubtle, 1.0f);
    // A new choice: the raised segment slides over from the old one, above the hover fills.
    const bool moving = m_move.moving() && m_selected >= 0 && m_selected < static_cast<int>(rects.size());
    if (moving) {
        for (std::size_t i = 0; i < rects.size(); ++i) {
            if (const float hot = m_hoverFade.amount(static_cast<int>(i)); hot > 0 && static_cast<int>(i) != m_selected) {
                canvas.fillRoundRect(segment(i), tokens::radius::menu, faded(Color::BgHover, hot));
            }
        }
        const RectF raised = m_move.at(segment(static_cast<std::size_t>(m_selected)), {b.x, b.y});
        canvas.fillRoundRect(raised, tokens::radius::menu, Color::AccentSubtle);
        canvas.strokeRoundRect(raised, tokens::radius::menu, Color::LineSubtle, 1.0f);
    }
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const RectF r = segment(i);
        const bool selected = static_cast<int>(i) == m_selected;
        const float hot = m_hoverFade.amount(static_cast<int>(i));
        if (!moving) {
            if (selected) {
                canvas.fillRoundRect(r, tokens::radius::menu, Color::AccentSubtle);
                canvas.strokeRoundRect(r, tokens::radius::menu, Color::LineSubtle, 1.0f);
            } else if (hot > 0) {
                canvas.fillRoundRect(r, tokens::radius::menu, faded(Color::BgHover, hot));
            }
        }
        canvas.drawText(m_options[i], r, selected ? TypeStyle::SmallSemi : TypeStyle::Small,
                        selected ? Ink(Color::TextPrimary) : blend(Color::TextSecondary, Color::TextPrimary, hot),
                        TextAlign::Center);
    }
}

void RadioGroup::choose(int index) {
    if (index < 0 || index >= static_cast<int>(m_options.size()) || index == m_selected) {
        return;
    }
    optionRects(m_rects);
    if (m_selected >= 0 && m_selected < static_cast<int>(m_rects.size())) {
        const RectF b = bounds();
        const float height = std::min(b.height, tokens::size::segment);
        RectF old = m_rects[static_cast<std::size_t>(m_selected)];
        old.y = b.y + (b.height - height) / 2 + kSegPad;
        old.height = height - 2 * kSegPad;
        if (m_move.start(m_selected, m_move.at(old, {b.x, b.y}), {b.x, b.y})) {
            animate();
        }
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
            return;
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

bool RadioGroup::tick(double now) {
    const bool hover = m_hoverFade.tick(now);
    const bool move = m_move.tick(now);
    invalidate();
    return hover || move;
}

} // namespace wl::ui
