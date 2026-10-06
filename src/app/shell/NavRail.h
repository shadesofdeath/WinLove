#pragma once
// Navigation rail per 03_components/navigation-rail.md: 200 / 44 wide, groups separated by
// 1px line.subtle, 24px items (icon 16, label, mono badge), active item bg.raised inset 4 + 2×12
// accent bar, footer "Daralt · Ctrl B". Keyboard: the rail is one Tab stop (the active item);
// ↑↓ move, Enter/Space select (roving focus). When the items outgrow the rail (short windows)
// they scroll under the wheel above the pinned footer, with an overlay ScrollBar.
#include "app/pages/PageInfo.h"
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class NavRail;
class NavList;

class NavItem : public ui::Widget {
public:
    NavItem(NavRail& rail, PageId page, std::wstring label, ui::icons::Icon icon);

    [[nodiscard]] PageId page() const noexcept { return m_page; }
    void setActive(bool active);
    void setBadge(int count);
    void setCollapsedTooltip(bool collapsed);

    void paint(ui::Canvas& canvas) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] ui::RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::r2; }

private:
    NavRail& m_rail;
    PageId m_page;
    std::wstring m_label;
    ui::icons::Icon m_icon;
    int m_badge = 0;
    bool m_active = false;
};

class NavFooter : public ui::Widget {
public:
    NavFooter(NavRail& rail, std::wstring collapseLabel, std::wstring expandLabel, std::wstring ctrlKey,
              std::wstring collapseTooltip);
    void paint(ui::Canvas& canvas) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;

private:
    NavRail& m_rail;
    std::wstring m_collapse;
    std::wstring m_expand;
    std::wstring m_ctrlKey;
};

class NavRail : public ui::Widget {
public:
    struct Labels {
        std::wstring collapse;
        std::wstring expand;
        std::wstring ctrlKey;
        std::wstring collapseTooltip;
    };
    // `label(Str)` resolves nav labels; items are created from allPages() with navGroup >= 0.
    NavRail(const Labels& labels, const std::function<std::wstring(Str)>& label);

    std::function<void(PageId)> onSelect;
    std::function<void()> onToggleCollapse;

    void setActive(PageId page);
    void setBadge(PageId page, int count);
    // 0 = fully collapsed (44), 1 = fully expanded (200); drives label fade during the animation.
    void setExpansion(float amount);
    [[nodiscard]] float expansion() const noexcept { return m_expansion; }
    [[nodiscard]] bool collapsed() const noexcept { return m_expansion < 0.5f; }
    void focusSibling(NavItem& from, int direction);
    // Scroll the item list (clamped); `reveal` brings an item fully into view.
    void scrollTo(float offset);
    void reveal(const NavItem& item);
    [[nodiscard]] float scrollOffset() const noexcept { return m_offset; }

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }

private:
    friend class NavList;
    NavList* m_list = nullptr;
    std::vector<NavItem*> m_items;
    std::vector<float> m_separators; // content y of group separators (before the scroll offset)
    NavFooter* m_footer = nullptr;
    ui::ScrollBar* m_scroll = nullptr;
    float m_content = 0;  // height of the item list
    float m_offset = 0;   // scroll offset of the item list
    float m_expansion = 1.0f;
};

} // namespace wl::app
