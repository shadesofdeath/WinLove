#pragma once
// D-109: the page navigation as the reference tool draws it — a card on the left (bg.chrome, radius r3, 200
// wide) with the groups Başlat · Entegre et · Kaldır · Yapılandır · Bitir. A group caption is a
// size::navLabel row in text.tertiary; an item is size::navItem high: a 16px icon in the accent color
// at 18, the label at 44 in text.primary. The chosen item sits on bg.pressed (its fill a spacing step
// shorter than the item, 7 in from the card's sides). The card is as tall as its items and is
// raised like the page card (elevation.card, D-136). Compact (Ctrl+B): icons only, 44 wide.
// 2026-11-09: when the window is not tall enough (a large density, a short window) the card scrolls
// — wheel or its overlay scroll bar — and clips its rows; the chosen page is kept in view.
// 2026-11-10 (UI polish): the hover fill fades and the chosen fill slides to a new item like the
// tabs' (ItemHover / SelectionMove; reduced motion: at once). What a page queues is a small badge
// at the right (icon high, accent-tinted; filled on the chosen item), "999+" past three digits.
#include "app/pages/PageInfo.h"
#include "ui/anim/ItemMotion.h"
#include "ui/widget/Widget.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class NavPanel : public ui::Widget {
public:
    struct Item {
        PageId page;
        std::wstring label;
        ui::icons::Icon icon;
        bool counted = true;
        bool finish = false; // the Uygula item: its icon in the success color, like the reference tool's
    };
    struct Group {
        std::wstring caption;
        std::vector<Item> items;
    };

    static constexpr float kCompactWidth = 44.0f;

    NavPanel();
    std::function<void(PageId)> onSelect;
    void setGroups(std::vector<Group> groups);
    void setActive(PageId page);
    void setCount(PageId page, int count);
    void setCompact(bool compact);
    [[nodiscard]] bool compact() const noexcept { return m_compact; }
    [[nodiscard]] float preferredWidth() const;
    [[nodiscard]] float preferredHeight() const;

    // A count as its badge says it: "7", "42", "318", past three digits "999+".
    [[nodiscard]] static std::wstring badgeText(int count);
    // Where `page`'s badge is drawn (empty: none — not counted, nothing queued, compact).
    [[nodiscard]] ui::RectF badgeRect(PageId page) const;
    // Where `page`'s label may draw (it starts at the same x whatever the badge).
    [[nodiscard]] ui::RectF labelRect(PageId page) const;
    // The chosen fill is on its way to a new item, or a hover fill fades (tests watch both end).
    [[nodiscard]] bool moving() const noexcept { return m_move.moving(); }
    [[nodiscard]] bool animating() const noexcept { return m_move.moving() || m_hoverFade.running(); }

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool tick(double now) override;
    void onPointerMove(ui::PointF p) override;
    bool onWheel(ui::PointF p, float lines) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] ui::Cursor cursor() const override;
    [[nodiscard]] ui::RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::r2; }
    [[nodiscard]] ui::RectF tooltipAnchor() const override; // compact: the item under the pointer

private:
    struct Placed {
        ui::RectF rect; // the size::navItem-high row
        int group = 0;
        int item = 0;
    };
    struct Caption {
        ui::RectF rect;
        int group = 0;
    };
    struct Badge {
        std::wstring text; // empty: none
        float width = 0;   // measured when the count is set (paint measures nothing)
    };
    void place();
    void scrollTo(float offset);
    void keepInView(int index);
    void measureBadges();
    void hoverTo(int index);
    [[nodiscard]] float maxScroll() const;
    [[nodiscard]] const Item& itemAt(const Placed& placed) const;
    [[nodiscard]] int indexOf(PageId page) const;
    [[nodiscard]] int count(PageId page) const;
    [[nodiscard]] ui::RectF badgeRect(const Placed& placed) const;
    [[nodiscard]] ui::PointF origin() const; // the rows' origin: scrolling takes a sliding fill along
    void select(int index);

    std::vector<Group> m_groups;
    std::vector<int> m_counts;     // by PageId
    std::vector<Badge> m_badges;   // by PageId
    std::vector<Placed> m_items;
    std::vector<Caption> m_captions;
    PageId m_active = PageId::Images;
    bool m_compact = false;
    int m_hover = -1;
    ui::ItemHover m_hoverFade;
    ui::SelectionMove m_move;
    float m_scroll = 0;              // how far the rows are moved up
    ui::ScrollBar* m_bar = nullptr;  // shown only when the rows do not fit
};

} // namespace wl::app
