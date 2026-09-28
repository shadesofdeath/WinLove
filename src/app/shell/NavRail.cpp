#include "app/shell/NavRail.h"

#include "ui/widget/Host.h"
#include "ui/widgets/Kbd.h"

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

bool isActivationKey(const ui::KeyEvent& key) {
    return key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN;
}

} // namespace

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
    m_badge = count;
    invalidate();
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
            canvas.drawText(badge, {b.right() - kBadgeRight - badgeWidth, b.y, badgeWidth + 1, b.height},
                            TypeStyle::Mono, ui::Ink(Color::TextTertiary, Color::TextTertiary, 0, expansion));
        }
    }
    if (m_badge > 0 && expansion < 1.0f) {
        // Collapsed: 6px accent dot at the icon's top-right.
        canvas.fillRoundRect({iconX + size::icon - kDotSize / 2, iconY - 1, kDotSize, kDotSize}, 1,
                             ui::Ink(Color::AccentBase, Color::AccentBase, 0, 1.0f - expansion));
    }
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
    for (const auto& page : allPages()) {
        if (page.navGroup >= 0) {
            m_items.push_back(&add<NavItem>(*this, page.id, label(page.navLabel), page.icon));
        }
    }
    m_footer = &add<NavFooter>(*this, labels.collapse, labels.expand, labels.ctrlKey, labels.collapseTooltip);
}

void NavRail::setActive(PageId page) {
    for (auto* item : m_items) {
        item->setActive(item->page() == page);
    }
}

void NavRail::setBadge(PageId page, int count) {
    for (auto* item : m_items) {
        if (item->page() == page) {
            item->setBadge(count);
        }
    }
}

void NavRail::setExpansion(float amount) {
    const bool wasCollapsed = collapsed();
    m_expansion = amount;
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
            const auto next = static_cast<std::ptrdiff_t>(i) + direction;
            if (next >= 0 && next < static_cast<std::ptrdiff_t>(m_items.size()) && host()) {
                host()->setFocus(m_items[static_cast<std::size_t>(next)], /*visible=*/true);
            }
            return;
        }
    }
}

void NavRail::layout() {
    const RectF b = bounds();
    const float row = size::row;
    float y = b.y + kPaddingY;
    int group = -1;
    m_separators.clear();
    for (auto* item : m_items) {
        const int itemGroup = pageInfo(item->page()).navGroup;
        if (itemGroup != group) {
            if (group >= 0) {
                y += kGroupPadding;
                m_separators.push_back(y);
                y += 1;
            }
            y += kGroupPadding;
            group = itemGroup;
        }
        item->setBounds({b.x, y, b.width, row});
        y += row;
    }
    y += kGroupPadding;
    m_separators.push_back(y);
    m_footer->setBounds({b.x, b.bottom() - kPaddingY - row, b.width, row});
}

void NavRail::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgPanel);
    canvas.hairlineV(b.right() - 1.0f / canvas.scale(), b.y, b.height, Color::LineSubtle);
    for (const float y : m_separators) {
        canvas.hairlineH(b.x, y, b.width - 1.0f / canvas.scale(), Color::LineSubtle);
    }
}

} // namespace wl::app
