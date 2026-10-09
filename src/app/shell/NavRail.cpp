#include "app/shell/NavRail.h"

#include "ui/widget/Host.h"
#include "ui/widgets/Kbd.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace size = ui::tokens::size;

namespace {

constexpr float kPaddingY = 8.0f;     // rail top/bottom padding
constexpr float kGroupPadding = 4.0f; // per group, above and below
constexpr float kItemPaddingLeft = 16.0f;
constexpr float kIconGap = 8.0f;
constexpr float kBadgeRight = 12.0f;
constexpr float kActiveInset = 4.0f;
constexpr float kDotSize = 6.0f;
constexpr float kHeaderHeight = 18.0f; // a group's header text row, the rail expanded
constexpr float kChevron = 12.0f;

// Height of a group's header: the separator (not before the first group) plus its text row,
// which shrinks away as the rail collapses to 44.
float headerHeight(int group, float expansion) {
    const float text = kHeaderHeight * std::clamp((expansion - 0.3f) / 0.7f, 0.0f, 1.0f);
    return group > 0 ? 2 * kGroupPadding + 1 + text : std::max(text, kGroupPadding);
}

bool isActivationKey(const ui::KeyEvent& key) {
    return key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN;
}

} // namespace

// ---- NavList -----------------------------------------------------------------------------------

// The scrolling viewport above the footer: clips the items (paint and hit test both stop at its
// bounds), draws the group separators and takes the wheel.
class NavList : public ui::Widget {
public:
    explicit NavList(NavRail& rail) : m_rail(rail) {}

    void paint(ui::Canvas& canvas) override {
        const RectF b = bounds();
        canvas.pushClip(b);
        for (const float y : m_rail.m_separators) {
            canvas.hairlineH(b.x, b.y + y - m_rail.m_offset, b.width - 1.0f / canvas.scale(), Color::LineSubtle);
        }
        canvas.popClip();
    }
    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    bool onWheel(ui::PointF /*p*/, float lines) override {
        if (m_rail.m_content <= bounds().height + 0.5f) {
            return false;
        }
        m_rail.scrollTo(m_rail.m_offset - lines * size::row);
        return true;
    }

private:
    NavRail& m_rail;
};

// ---- NavItem -----------------------------------------------------------------------------------

NavItem::NavItem(NavRail& rail, PageId page, std::wstring label, ui::icons::Icon icon)
    : m_rail(rail), m_page(page), m_label(std::move(label)), m_icon(icon) {
    setFocusable(true);
    setTabStop(false); // only the active item is a Tab stop (roving focus)
    setAccessible(ui::AccessRole::TabItem, m_label);
}

void NavItem::setActive(bool active) {
    m_active = active;
    setTabStop(active);
    invalidate();
}

void NavItem::setBadge(int count) {
    if (count > m_badge) {
        m_pulse.snapTo(1.0f);
        if (m_pulse.animateTo(0.0f, 600.0f, ui::tokens::motion::standard)) {
            animate();
        }
    }
    m_badge = count;
    invalidate();
}

bool NavItem::tick(double now) {
    const bool running = m_pulse.tick(now);
    invalidate();
    return running;
}

void NavItem::setCollapsedTooltip(bool collapsed) {
    // navigation-rail.md: collapsed rail shows "label · badge" as tooltip.
    std::wstring text;
    if (collapsed) {
        text = m_badge > 0 ? m_label + L" · " + std::to_wstring(m_badge) : m_label;
    }
    setTooltip(std::move(text));
}

ui::RectF NavItem::focusRect() const {
    const RectF b = bounds();
    return {b.x + kActiveInset, b.y, b.width - 2 * kActiveInset, b.height};
}

void NavItem::onClick() {
    if (m_rail.onSelect) {
        m_rail.onSelect(m_page);
    }
}

bool NavItem::onKeyDown(const ui::KeyEvent& key) {
    if (isActivationKey(key)) {
        onClick();
        return true;
    }
    if (key.virtualKey == VK_UP || key.virtualKey == VK_DOWN) {
        m_rail.focusSibling(*this, key.virtualKey == VK_UP ? -1 : 1);
        return true;
    }
    return false;
}

void NavItem::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float expansion = m_rail.expansion();
    if (m_active) {
        canvas.fillRoundRect({b.x + kActiveInset, b.y, b.width - 2 * kActiveInset, b.height}, ui::tokens::radius::r2,
                             Color::BgRaised);
        canvas.fillRect({b.x + kActiveInset, b.y + (b.height - 12) / 2, 2, 12}, Color::AccentBase);
    }
    const Color ink = m_active || hovered() ? Color::TextPrimary : Color::TextSecondary;

    // Icon slides from the expanded position (x 16) to centered in 44 as the rail collapses.
    const float expandedX = kItemPaddingLeft;
    const float collapsedX = (size::navCollapsed - size::icon) / 2;
    const float iconX = b.x + collapsedX + (expandedX - collapsedX) * expansion;
    const float iconY = b.y + (b.height - size::icon) / 2;
    // Active items keep the outline icon: the handoff screens draw it that way, and several filled
    // variants (e.g. source) become solid blocks at 16px (DECISIONS D-014).
    canvas.drawIcon(m_icon, {iconX, iconY}, ink);

    if (expansion > 0.01f) {
        const float labelX = iconX + size::icon + kIconGap;
        std::wstring badge = m_badge > 0 ? std::to_wstring(m_badge) : std::wstring{};
        const float badgeWidth = badge.empty() ? 0.0f : std::ceil(canvas.text().measure(badge, TypeStyle::Mono));
        const float labelRight = b.right() - kBadgeRight - (badgeWidth > 0 ? badgeWidth + kIconGap : 0.0f);
        canvas.drawText(m_label, {labelX, b.y, std::max(labelRight - labelX, 0.0f), b.height}, TypeStyle::Body,
                        ui::Ink(ink, ink, 0, expansion));
        if (!badge.empty()) {
            const float pulse = m_pulse.value();
            if (pulse > 0.01f) {
                canvas.fillRoundRect({b.right() - kBadgeRight - badgeWidth - 4, b.y + 4, badgeWidth + 8, b.height - 8},
                                     ui::tokens::radius::r2, ui::Ink(Color::AccentSubtle, Color::AccentSubtle, 0, pulse * expansion));
            }
            canvas.drawText(badge, {b.right() - kBadgeRight - badgeWidth, b.y, badgeWidth + 1, b.height}, TypeStyle::Mono,
                            ui::Ink(Color::TextTertiary, Color::AccentBase, pulse, expansion));
        }
    }
    if (m_badge > 0 && expansion < 1.0f) {
        // Collapsed: 6px accent dot at the icon's top-right.
        canvas.fillRoundRect({iconX + size::icon - kDotSize / 2, iconY - 1, kDotSize, kDotSize}, 1,
                             ui::Ink(Color::AccentBase, Color::AccentBase, 0, 1.0f - expansion));
    }
}

// ---- NavGroupHeader ----------------------------------------------------------------------------

NavGroupHeader::NavGroupHeader(NavRail& rail, int group, std::wstring label, std::wstring tooltip)
    : m_rail(rail), m_group(group), m_label(std::move(label)) {
    setFocusable(true);
    setTabStop(false);
    setTooltip(std::move(tooltip));
    setAccessible(ui::AccessRole::Button, m_label);
}

void NavGroupHeader::onClick() {
    if (!m_rail.collapsed() && m_rail.onToggleGroup) {
        m_rail.onToggleGroup(m_group);
    }
}

bool NavGroupHeader::onKeyDown(const ui::KeyEvent& key) {
    if (isActivationKey(key)) {
        onClick();
        return true;
    }
    return false;
}

void NavGroupHeader::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    if (m_group > 0) {
        canvas.hairlineH(b.x, b.y + kGroupPadding, b.width - 1.0f / canvas.scale(), Color::LineSubtle);
    }
    const float expansion = m_rail.expansion();
    const float textTop = b.y + (m_group > 0 ? 2 * kGroupPadding + 1 : 0.0f);
    if (expansion <= 0.3f || b.bottom() - textTop < 8.0f) {
        return; // collapsed rail: the line alone, as before the headers
    }
    const bool closed = m_rail.groupClosed(m_group);
    const Color ink = hovered() ? Color::TextSecondary : Color::TextTertiary;
    const ui::Ink faded(ink, ink, 0, expansion);
    const RectF row{b.x + kItemPaddingLeft, textTop, b.width - kItemPaddingLeft - kBadgeRight, b.bottom() - textTop};
    const float chevronX = row.right() - kChevron;
    canvas.drawIcon(closed ? ui::icons::Icon::ChevronRight : ui::icons::Icon::ChevronDown,
                    {chevronX, row.y + (row.height - kChevron) / 2}, faded, ui::IconVariant::Regular16, kChevron);
    float right = chevronX - 4.0f;
    if (closed && m_badge > 0) {
        const std::wstring badge = std::to_wstring(m_badge);
        const float w = std::ceil(canvas.text().measure(badge, TypeStyle::Mono));
        canvas.drawText(badge, {right - w, row.y, w + 1, row.height}, TypeStyle::Mono,
                        ui::Ink(Color::AccentBase, Color::AccentBase, 0, expansion));
        right -= w + 6.0f;
    }
    canvas.drawText(m_label, {row.x, row.y, std::max(right - row.x, 0.0f), row.height}, TypeStyle::Section, faded);
}

// ---- NavFooter ---------------------------------------------------------------------------------

NavFooter::NavFooter(NavRail& rail, std::wstring collapseLabel, std::wstring expandLabel, std::wstring ctrlKey,
                     std::wstring collapseTooltip)
    : m_rail(rail), m_collapse(std::move(collapseLabel)), m_expand(std::move(expandLabel)),
      m_ctrlKey(std::move(ctrlKey)) {
    setFocusable(true);
    setTooltip(std::move(collapseTooltip));
    setAccessible(ui::AccessRole::Button, m_collapse);
}

void NavFooter::onClick() {
    if (m_rail.onToggleCollapse) {
        m_rail.onToggleCollapse();
    }
}

bool NavFooter::onKeyDown(const ui::KeyEvent& key) {
    if (isActivationKey(key)) {
        onClick();
        return true;
    }
    return false;
}

void NavFooter::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const float expansion = m_rail.expansion();
    const Color ink = hovered() ? Color::TextSecondary : Color::TextTertiary;
    const float expandedX = kItemPaddingLeft;
    const float collapsedX = (size::navCollapsed - size::icon) / 2;
    const float iconX = b.x + collapsedX + (expandedX - collapsedX) * expansion;
    canvas.drawIcon(ui::icons::Icon::SidebarToggle, {iconX, b.y + (b.height - size::icon) / 2}, ink);
    if (expansion > 0.01f) {
        const float labelX = iconX + size::icon + kIconGap;
        const std::vector<std::wstring> keys{m_ctrlKey, L"B"};
        const float keysLeft = ui::Kbd::paintKeys(canvas, keys, b.right() - kBadgeRight, b.y + b.height / 2);
        canvas.drawText(m_rail.collapsed() ? m_expand : m_collapse,
                        {labelX, b.y, std::max(keysLeft - labelX - kIconGap, 0.0f), b.height}, TypeStyle::Body,
                        ui::Ink(ink, ink, 0, expansion));
    }
}

// ---- NavRail -----------------------------------------------------------------------------------

NavRail::NavRail(const Labels& labels, const std::function<std::wstring(Str)>& label) {
    setAccessible(ui::AccessRole::List, L"Navigation");
    m_list = &add<NavList>(*this);
    int group = -1;
    for (const auto& page : allPages()) {
        if (page.navGroup < 0) {
            continue;
        }
        if (page.navGroup != group) {
            group = page.navGroup;
            m_headers.push_back(&m_list->add<NavGroupHeader>(*this, group, label(navGroupLabel(group)),
                                                             label(Str::NavGroupFold)));
        }
        m_items.push_back(&m_list->add<NavItem>(*this, page.id, label(page.navLabel), page.icon));
    }
    m_scroll = &m_list->add<ui::ScrollBar>(); // added last: above the items
    m_scroll->onScroll = [this](float offset) { scrollTo(offset); };
    m_footer = &add<NavFooter>(*this, labels.collapse, labels.expand, labels.ctrlKey, labels.collapseTooltip);
}

void NavRail::setActive(PageId page) {
    for (auto* item : m_items) {
        item->setActive(item->page() == page);
    }
    layout(); // a folded group shows its active page
    invalidate();
}

void NavRail::setClosedGroups(std::vector<int> groups) {
    m_closed = std::move(groups);
    layout();
    invalidate();
}

int NavRail::badge(PageId page) const {
    for (const auto* item : m_items) {
        if (item->page() == page) {
            return item->badge();
        }
    }
    return 0;
}

bool NavRail::groupClosed(int group) const {
    return std::ranges::find(m_closed, group) != m_closed.end();
}

void NavRail::setBadge(PageId page, int count) {
    for (auto* item : m_items) {
        if (item->page() == page && item->badge() != count) {
            item->setBadge(count);
            layout(); // a folded group's header sums its pages' badges
        }
    }
}

void NavRail::setExpansion(float amount) {
    const bool wasCollapsed = collapsed();
    m_expansion = amount;
    layout(); // headers shrink to separators; folded groups open while the rail is narrow
    if (wasCollapsed != collapsed() || amount == 0.0f || amount == 1.0f) {
        for (auto* item : m_items) {
            item->setCollapsedTooltip(collapsed());
        }
    }
    invalidate();
}

void NavRail::focusSibling(NavItem& from, int direction) {
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i] == &from) {
            auto next = static_cast<std::ptrdiff_t>(i) + direction;
            while (next >= 0 && next < static_cast<std::ptrdiff_t>(m_items.size()) &&
                   !m_items[static_cast<std::size_t>(next)]->visible()) {
                next += direction; // past the pages of a folded group
            }
            if (next >= 0 && next < static_cast<std::ptrdiff_t>(m_items.size()) && host()) {
                NavItem* target = m_items[static_cast<std::size_t>(next)];
                reveal(*target);
                host()->setFocus(target, /*visible=*/true);
            }
            return;
        }
    }
}

void NavRail::scrollTo(float offset) {
    const float viewport = m_list ? m_list->bounds().height : 0.0f;
    offset = std::clamp(offset, 0.0f, std::max(m_content - viewport, 0.0f));
    if (offset != m_offset) {
        m_offset = offset;
        layout();
        invalidate();
    }
}

void NavRail::reveal(const NavItem& item) {
    const RectF list = m_list->bounds();
    const RectF r = item.bounds();
    if (r.y < list.y) {
        scrollTo(m_offset - (list.y - r.y));
    } else if (r.bottom() > list.bottom()) {
        scrollTo(m_offset + r.bottom() - list.bottom());
    }
}

void NavRail::layout() {
    const RectF b = bounds();
    const float row = size::row;
    // Content coordinates (0 = top of the list); the list scrolls when it is taller than the
    // space above the footer.
    float y = kPaddingY;
    int group = -1;
    m_separators.clear();
    std::vector<float> tops(m_items.size(), 0.0f);
    std::vector<float> headerTops(m_headers.size(), 0.0f);
    // The rail at 44: no labels and every page reachable — nothing is folded then.
    const bool folding = !collapsed();
    std::size_t header = 0;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        NavItem* item = m_items[i];
        const int itemGroup = pageInfo(item->page()).navGroup;
        if (itemGroup != group) {
            group = itemGroup;
            int badges = 0;
            for (auto* other : m_items) {
                badges += pageInfo(other->page()).navGroup == group ? other->badge() : 0;
            }
            m_headers[header]->setBadge(badges);
            headerTops[header] = y;
            y += headerHeight(group, m_expansion);
            ++header;
        }
        const bool shown = !folding || !groupClosed(itemGroup) || item->active();
        item->setVisible(shown);
        tops[i] = y;
        if (shown) {
            y += row;
        }
    }
    y += kGroupPadding;
    m_separators.push_back(y);
    m_content = y + 1;

    const float footerTop = b.bottom() - kPaddingY - row;
    m_footer->setBounds({b.x, footerTop, b.width, row});
    const RectF list{b.x, b.y, b.width, std::max(footerTop - kGroupPadding - b.y, 0.0f)};
    m_list->setBounds(list);
    m_offset = std::clamp(m_offset, 0.0f, std::max(m_content - list.height, 0.0f));
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        m_items[i]->setBounds({b.x, list.y + tops[i] - m_offset, b.width, row});
    }
    for (std::size_t h = 0; h < m_headers.size(); ++h) {
        m_headers[h]->setBounds({b.x, list.y + headerTops[h] - m_offset, b.width,
                                 headerHeight(m_headers[h]->group(), m_expansion)});
        m_headers[h]->setEnabled(folding);
    }
    m_scroll->setBounds({list.right() - ui::ScrollBar::kWidth, list.y, ui::ScrollBar::kWidth, list.height});
    m_scroll->setRange(m_content, list.height);
    m_scroll->setOffset(m_offset);
    m_scroll->setVisible(m_scroll->needed());
}

void NavRail::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineV(b.right() - 1.0f / canvas.scale(), b.y, b.height, Color::LineSubtle);
    if (m_scroll->needed()) {
        // Scrolled list: a fixed line keeps the footer apart from the items passing under it.
        canvas.hairlineH(b.x, m_list->bounds().bottom(), b.width - 1.0f / canvas.scale(), Color::LineSubtle);
    }
}

} // namespace wl::app
