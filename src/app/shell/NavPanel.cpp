#include "app/shell/NavPanel.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {

// D-109 (the reference tool at 175 %), in DIP: the caption text 8 in, the icon at 18, the label at 44, the
// chosen fill 7 in from the card's sides, 10 between groups. D-136: the fill follows the item's
// height (a spacing step less, so two neighbouring fills never touch), the last item keeps the
// card's inner padding under it.
constexpr float kCaptionX = 8.0f;
constexpr float kIconX = 18.0f;
constexpr float kLabelX = 44.0f;
constexpr float kCountRight = 14.0f;
constexpr float kFillInset = 7.0f;
constexpr float kFillHeight = size::navItem - ui::tokens::spacing::s2;
// D-137: the chosen item's accent bar, inside the fill's left edge (TableView's chosen row has one too).
constexpr float kMarkWidth = 3.0f;
constexpr float kMarkInsetY = 8.0f;
constexpr float kGroupGap = 10.0f;
constexpr float kBottom = size::panelPad;
constexpr float kCompactGroupGap = 8.0f;
// The count badge: as high as the item's icon (so the two frame the row alike), the digits 4 in.
constexpr float kBadgeHeight = size::icon;
constexpr float kBadgePadX = ui::tokens::spacing::s2;
constexpr float kBadgeGap = ui::tokens::spacing::s4; // between the label's room and the badge
constexpr int kBadgeMost = 999;

// The chosen item's fill: inset in its row.
RectF fillOf(RectF row) {
    return {row.x + kFillInset, row.y + std::round((row.height - kFillHeight) / 2), std::max(row.width - 2 * kFillInset, 0.0f),
            kFillHeight};
}

ui::Ink faded(Color color, float opacity) {
    return ui::Ink(color, color, 0.0f, opacity);
}

} // namespace

NavPanel::NavPanel()
    : m_counts(static_cast<std::size_t>(PageId::Count), 0), m_badges(static_cast<std::size_t>(PageId::Count)) {
    setFocusable(true);
    setAccessible(ui::AccessRole::TabItem, L"");
    m_bar = &add<ui::ScrollBar>();
    m_bar->setVisible(false);
    m_bar->onScroll = [this](float offset) { scrollTo(offset); };
}

std::wstring NavPanel::badgeText(int count) {
    if (count <= 0) {
        return {};
    }
    return count > kBadgeMost ? std::to_wstring(kBadgeMost) + L"+" : std::to_wstring(count);
}

float NavPanel::maxScroll() const {
    return std::max(preferredHeight() - bounds().height, 0.0f);
}

ui::PointF NavPanel::origin() const {
    return {bounds().x, bounds().y - m_scroll};
}

void NavPanel::scrollTo(float offset) {
    const float next = std::clamp(std::round(offset), 0.0f, maxScroll());
    if (next != m_scroll) {
        m_scroll = next;
        m_bar->setOffset(m_scroll);
        place();
        invalidate();
    }
}

void NavPanel::keepInView(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return;
    }
    const RectF b = bounds();
    const RectF r = m_items[static_cast<std::size_t>(index)].rect;
    if (r.y < b.y) {
        scrollTo(m_scroll - (b.y - r.y) - size::navLabel); // its group caption too
    } else if (r.bottom() > b.bottom()) {
        scrollTo(m_scroll + (r.bottom() - b.bottom()) + kBottom);
    }
}

bool NavPanel::onWheel(ui::PointF p, float lines) {
    if (maxScroll() <= 0) {
        return false;
    }
    scrollTo(m_scroll - lines * size::navItem);
    onPointerMove(p); // no mouse-move follows a wheel: the row under the pointer changed
    return true;
}

void NavPanel::setGroups(std::vector<Group> groups) {
    m_groups = std::move(groups);
    // Other rows altogether (another menu of the title bar): nothing slides or fades across.
    m_hover = -1;
    m_hoverFade.snap(-1);
    m_move.stop();
    layout(); // the scroll range follows the rows
    invalidate();
}

void NavPanel::setActive(PageId page) {
    if (page != m_active) {
        m_move.stop(); // chosen by code (a shortcut, the palette): the fill is simply there
    }
    // The same item again (the shell showing the page this panel's own click chose) lets the slide go on.
    m_active = page;
    if (const int i = indexOf(page); i >= 0) {
        setAccessible(ui::AccessRole::TabItem, itemAt(m_items[static_cast<std::size_t>(i)]).label);
        keepInView(i);
    }
    invalidate();
}

void NavPanel::setCount(PageId page, int n) {
    const auto i = static_cast<std::size_t>(page);
    if (i < m_counts.size() && m_counts[i] != n) {
        m_counts[i] = n;
        Badge& badge = m_badges[i];
        badge.text = badgeText(n);
        badge.width = badge.text.empty() ? 0.0f
                                         : std::max(kBadgeHeight, textWidth(badge.text, TypeStyle::Badge) + 2 * kBadgePadX);
        invalidate();
    }
}

void NavPanel::measureBadges() {
    // Counts set before the panel had a host (the shell's first page) were measured as nothing.
    for (Badge& badge : m_badges) {
        if (!badge.text.empty()) {
            badge.width = std::max(kBadgeHeight, textWidth(badge.text, TypeStyle::Badge) + 2 * kBadgePadX);
        }
    }
}

int NavPanel::count(PageId page) const {
    const auto i = static_cast<std::size_t>(page);
    return i < m_counts.size() ? m_counts[i] : 0;
}

void NavPanel::setCompact(bool compact) {
    if (compact != m_compact) {
        m_compact = compact;
        m_move.stop();
        layout();
        invalidate();
    }
}

float NavPanel::preferredWidth() const {
    return m_compact ? kCompactWidth : size::nav;
}

float NavPanel::preferredHeight() const {
    float h = 0;
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        if (g > 0) {
            h += m_compact ? kCompactGroupGap : kGroupGap;
        }
        h += (m_compact ? 4.0f : size::navLabel) + static_cast<float>(m_groups[g].items.size()) * size::navItem;
    }
    return h + kBottom;
}

void NavPanel::place() {
    m_items.clear();
    m_captions.clear();
    const RectF b = bounds();
    float y = b.y - m_scroll;
    for (std::size_t g = 0; g < m_groups.size(); ++g) {
        if (g > 0) {
            y += m_compact ? kCompactGroupGap : kGroupGap;
        }
        const float caption = m_compact ? 4.0f : size::navLabel;
        m_captions.push_back({{b.x + kCaptionX, y, std::max(b.width - 2 * kCaptionX, 0.0f), caption}, static_cast<int>(g)});
        y += caption;
        for (std::size_t i = 0; i < m_groups[g].items.size(); ++i) {
            m_items.push_back({{b.x, y, b.width, size::navItem}, static_cast<int>(g), static_cast<int>(i)});
            y += size::navItem;
        }
    }
}

void NavPanel::layout() {
    const RectF b = bounds();
    measureBadges();
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll());
    m_bar->setRange(preferredHeight(), b.height);
    m_bar->setOffset(m_scroll);
    m_bar->setVisible(m_bar->needed());
    m_bar->setBounds({b.right() - ui::ScrollBar::kWidth, b.y + 4, ui::ScrollBar::kWidth, std::max(b.height - 8, 0.0f)});
    place();
    keepInView(indexOf(m_active));
}

const NavPanel::Item& NavPanel::itemAt(const Placed& placed) const {
    return m_groups[static_cast<std::size_t>(placed.group)].items[static_cast<std::size_t>(placed.item)];
}

int NavPanel::indexOf(PageId page) const {
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (itemAt(m_items[i]).page == page) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

RectF NavPanel::badgeRect(const Placed& placed) const {
    const Item& item = itemAt(placed);
    const auto i = static_cast<std::size_t>(item.page);
    if (m_compact || !item.counted || i >= m_badges.size() || m_badges[i].text.empty()) {
        return {};
    }
    const RectF r = placed.rect;
    const float w = m_badges[i].width;
    return {r.right() - kCountRight - w, r.y + std::round((r.height - kBadgeHeight) / 2), w, kBadgeHeight};
}

RectF NavPanel::badgeRect(PageId page) const {
    const int i = indexOf(page);
    return i < 0 ? RectF{} : badgeRect(m_items[static_cast<std::size_t>(i)]);
}

RectF NavPanel::labelRect(PageId page) const {
    const int i = indexOf(page);
    if (i < 0) {
        return {};
    }
    const RectF r = m_items[static_cast<std::size_t>(i)].rect;
    const RectF badge = badgeRect(m_items[static_cast<std::size_t>(i)]);
    const float right = badge.width > 0 ? badge.x - kBadgeGap : r.right() - kCountRight;
    return {r.x + kLabelX, r.y, std::max(right - r.x - kLabelX, 0.0f), r.height};
}

void NavPanel::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.raisedCard(b, ui::tokens::radius::r3, Color::BgChrome); // D-136: raised like the page card
    canvas.pushClip(b); // rows scrolled out of the card are not drawn over the window
    if (!m_compact) {
        for (const auto& caption : m_captions) {
            canvas.drawText(m_groups[static_cast<std::size_t>(caption.group)].caption, caption.rect, TypeStyle::GroupCap,
                            Color::TextTertiary);
        }
    } else {
        for (std::size_t i = 1; i < m_captions.size(); ++i) {
            const RectF c = m_captions[i].rect;
            canvas.hairlineH(b.x + 10, c.y - kCompactGroupGap / 2, b.width - 20, Color::LineStrong);
        }
    }
    const int active = indexOf(m_active);
    const bool moving = m_move.moving() && active >= 0;
    const int from = m_move.from();
    const float t = m_move.progress();
    // Hover fills first (they fade), then the chosen fill — sliding over them to a new item.
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const int index = static_cast<int>(i);
        if (index == active && !moving) {
            continue; // the chosen item at rest shows its own fill only
        }
        if (const float hot = m_hoverFade.amount(index); hot > 0) {
            canvas.fillRoundRect(fillOf(m_items[i].rect), ui::tokens::radius::r2, faded(Color::BgHover, hot));
        }
    }
    if (active >= 0) {
        // D-137: the chosen item is a selection as a table's chosen row is — the selection fill and a
        // short accent bar on its left (was the pressed grey); the bar rides with the fill.
        const RectF to = fillOf(m_items[static_cast<std::size_t>(active)].rect);
        const RectF fill = moving ? m_move.at(to, origin()) : to;
        canvas.fillRoundRect(fill, ui::tokens::radius::r2, Color::AccentSubtle);
        canvas.fillRoundRect({fill.x, fill.y + kMarkInsetY, kMarkWidth, std::max(fill.height - 2 * kMarkInsetY, 0.0f)},
                             kMarkWidth / 2, Color::AccentBase);
    }
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const Placed& placed = m_items[i];
        const Item& item = itemAt(placed);
        const RectF r = placed.rect;
        const float iconX = m_compact ? r.x + std::round((r.width - size::icon) / 2) : r.x + kIconX;
        canvas.drawIcon(item.icon, {iconX, r.y + std::round((r.height - size::icon) / 2)},
                        item.finish ? Color::StatusSuccess : Color::AccentBase);
        if (m_compact) {
            continue;
        }
        float right = r.right() - kCountRight;
        if (const RectF badge = badgeRect(placed); badge.width > 0) {
            // Accent-tinted at rest, filled on the chosen item; the two trade places as the fill slides.
            const int index = static_cast<int>(i);
            const float chosen = index == active ? (moving ? t : 1.0f) : (moving && index == from ? 1.0f - t : 0.0f);
            // D-137: at rest the selection fill (the accent text reads on it at >= 4.6:1; a 20 % accent
            // over the light card read at ~4.2).
            canvas.fillRoundRect(badge, ui::tokens::radius::badge, ui::Ink(Color::AccentSubtle, Color::AccentBase, chosen));
            canvas.drawText(m_badges[static_cast<std::size_t>(item.page)].text, badge, TypeStyle::Badge,
                            ui::Ink(Color::AccentText, Color::TextOnAccent, chosen), ui::TextAlign::Center);
            right = badge.x - kBadgeGap;
        }
        canvas.drawText(item.label, {r.x + kLabelX, r.y, std::max(right - r.x - kLabelX, 0.0f), r.height}, TypeStyle::Body,
                        Color::TextPrimary);
    }
    canvas.popClip();
}

bool NavPanel::tick(double now) {
    const bool hover = m_hoverFade.tick(now);
    const bool move = m_move.tick(now);
    invalidate();
    return hover || move;
}

void NavPanel::hoverTo(int index) {
    if (index == m_hover) {
        return;
    }
    m_hover = index;
    // Compact: every item says its name; D-136: a label cut to "…" by its count badge says it whole.
    std::wstring tip;
    if (index >= 0) {
        const Item& item = itemAt(m_items[static_cast<std::size_t>(index)]);
        if (m_compact || textWidth(item.label, TypeStyle::Body) > labelRect(item.page).width) {
            tip = item.label;
        }
    }
    setTooltip(std::move(tip));
    if (m_hoverFade.set(index)) {
        animate();
    }
    invalidate();
}

void NavPanel::onPointerMove(ui::PointF p) {
    int hover = -1;
    const bool overBar = m_bar->visible() && m_bar->bounds().contains(p);
    for (std::size_t i = 0; i < m_items.size() && bounds().contains(p) && !overBar; ++i) {
        if (m_items[i].rect.contains(p)) {
            hover = static_cast<int>(i);
        }
    }
    hoverTo(hover);
}

void NavPanel::onHoverChanged(bool hovered) {
    if (!hovered) {
        hoverTo(-1); // no pointer move arrives after the pointer has left the card
    }
    invalidate();
}

ui::Cursor NavPanel::cursor() const {
    return m_hover >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow;
}

void NavPanel::select(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        return;
    }
    const PageId page = itemAt(m_items[static_cast<std::size_t>(index)]).page;
    if (const int current = indexOf(m_active); current >= 0 && current != index) {
        // From where the fill is now: a choice made while it still slides goes on from there.
        const RectF now = m_move.at(fillOf(m_items[static_cast<std::size_t>(current)].rect), origin());
        if (m_move.start(current, now, origin())) {
            animate();
        }
    }
    m_active = page; // the shell's setActive(page) that follows keeps the slide
    invalidate();
    if (onSelect) {
        auto handler = onSelect; // the shell rebuilds the page (and may rebuild these groups)
        handler(page);
    }
}

void NavPanel::onClick() {
    if (m_hover >= 0) {
        select(m_hover);
    }
}

bool NavPanel::onKeyDown(const ui::KeyEvent& key) {
    const int current = indexOf(m_active);
    const int last = static_cast<int>(m_items.size()) - 1;
    int next = current;
    switch (key.virtualKey) {
    case VK_UP: next = std::max(current - 1, 0); break;
    case VK_DOWN: next = std::min(current + 1, last); break;
    case VK_HOME: next = 0; break;
    case VK_END: next = last; break;
    default: return false;
    }
    if (next != current) {
        select(next);
    }
    return true;
}

RectF NavPanel::tooltipAnchor() const {
    return m_hover >= 0 && m_hover < static_cast<int>(m_items.size()) ? fillOf(m_items[static_cast<std::size_t>(m_hover)].rect)
                                                                         : bounds();
}

RectF NavPanel::focusRect() const {
    const int i = indexOf(m_active);
    if (i < 0) {
        return bounds();
    }
    return fillOf(m_items[static_cast<std::size_t>(i)].rect);
}

} // namespace wl::app
