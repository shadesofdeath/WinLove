#pragma once
// The grouped tabs of the WinLoveUI package (D-107, docs/LAYOUT.md "Sekmeler"): each group has a
// caption in caps (GroupCap, text.tertiary, 16 high) over its tabs (40 high, 18 apart); groups
// are parted by a 1px line with 18 on each side. A tab is BodyMedium text.secondary, the chosen
// one BodySemi text.primary over a 2px accent line; a count follows the title (accent text when
// above 0, text.tertiary at 0). The package shows ten tabs; WinLove has more pages, so groups
// that do not fit flow onto another row (a group wider than the strip continues on the next
// row without its caption) — every page stays one click away.
#include "app/pages/PageInfo.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::app {

class TabStrip : public ui::Widget {
public:
    static constexpr float kRowHeight = ui::tokens::size::groupCaption + ui::tokens::size::tab; // 56
    static constexpr float kRowGap = 4.0f;

    struct Tab {
        PageId page;
        std::wstring label;
        bool counted = true; // shows a count (0 too)
    };
    struct Group {
        std::wstring caption;
        std::vector<Tab> tabs;
    };

    TabStrip();
    std::function<void(PageId)> onSelect;
    void setGroups(std::vector<Group> groups);
    void setActive(PageId page);
    void setCount(PageId page, int count);
    [[nodiscard]] int count(PageId page) const;
    // Height the groups take at `width` (rows of 56, 6 apart).
    [[nodiscard]] float heightFor(float width) const;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    [[nodiscard]] ui::Cursor cursor() const override;
    [[nodiscard]] ui::RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override { return 4.0f; }

private:
    struct Placed {
        ui::RectF rect;     // the tab (text + count), 40 high
        int group = 0;
        int tab = 0;
    };
    struct Caption {
        ui::RectF rect;
        int group = 0;
    };
    struct Flow {
        std::vector<Placed> tabs;
        std::vector<Caption> captions;
        std::vector<ui::RectF> separators;
        float height = 0;
    };
    [[nodiscard]] Flow flow(float x0, float y0, float width) const;
    [[nodiscard]] float tabWidth(const Tab& tab) const;
    [[nodiscard]] const Tab& tabAt(const Placed& placed) const;
    [[nodiscard]] int indexOf(PageId page) const; // in m_flow.tabs; -1 when not shown
    void select(int index);

    std::vector<Group> m_groups;
    std::vector<int> m_counts; // by PageId
    PageId m_active = PageId::Source;
    Flow m_flow;
    int m_hover = -1;
};

} // namespace wl::app
