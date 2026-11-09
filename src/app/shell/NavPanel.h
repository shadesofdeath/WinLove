#pragma once
// D-109: the page navigation as the reference tool draws it — a card on the left (bg.chrome, radius r3, 200
// wide) with the groups Başlat · Entegre et · Kaldır · Yapılandır · Bitir. A group caption is a
// 25-high row in text.tertiary; an item is 28 high: a 16px icon in the accent color at 18, the
// label at 44 in text.primary, what the page queues right-aligned in text.secondary. The chosen
// item sits on bg.pressed (23 high, 7 in from the card's sides). The card is as tall as its items.
// Compact (Ctrl+B): icons only, 44 wide.
// 2026-11-09: when the window is not tall enough (a large density, a short window) the card scrolls
// — wheel or its overlay scroll bar — and clips its rows; the chosen page is kept in view.
#include "app/pages/PageInfo.h"
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

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    bool onWheel(ui::PointF p, float lines) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] ui::Cursor cursor() const override;
    [[nodiscard]] ui::RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override { return ui::tokens::radius::r2; }

private:
    struct Placed {
        ui::RectF rect; // the 28-high row
        int group = 0;
        int item = 0;
    };
    struct Caption {
        ui::RectF rect;
        int group = 0;
    };
    void place();
    void scrollTo(float offset);
    void keepInView(int index);
    [[nodiscard]] float maxScroll() const;
    [[nodiscard]] const Item& itemAt(const Placed& placed) const;
    [[nodiscard]] int indexOf(PageId page) const;
    [[nodiscard]] int count(PageId page) const;
    void select(int index);

    std::vector<Group> m_groups;
    std::vector<int> m_counts; // by PageId
    std::vector<Placed> m_items;
    std::vector<Caption> m_captions;
    PageId m_active = PageId::Images;
    bool m_compact = false;
    int m_hover = -1;
    float m_scroll = 0;              // how far the rows are moved up
    ui::ScrollBar* m_bar = nullptr;  // shown only when the rows do not fit
};

} // namespace wl::app
