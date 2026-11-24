#pragma once
// TabBar and RadioGroup per the WinLoveUI package (D-107).
// TabBar (Painter::Tab): text tabs 18 apart; selected = BodySemi text.primary + 2px accent
// underline; others BodyMedium text.secondary; counts in accent text; 1px line under the bar.
// ←/→ move, click selects. The underline slides to the chosen tab and its pill fades over.
// RadioGroup (Painter::Segmented): one of a few choices as a 34px segmented control — sunk track,
// the chosen segment on the panel color with a line border. ←/→ move the selection; the raised
// segment slides to it. Hover highlights fade like a Button's (ItemHover).
#include "ui/anim/ItemMotion.h"
#include "ui/widget/Widget.h"
#include "ui/widgets/StripScroll.h"

#include <algorithm>

#include <functional>
#include <string>
#include <vector>

namespace wl::ui {

// D-136: tabs wider than the bar scroll sideways (StripScroll): the wheel, a chevron at an edge
// that hides tabs, and the chosen tab always brought whole into view.
class TabBar : public Widget {
public:
    explicit TabBar(std::vector<std::wstring> tabs, int selected = 0);
    std::function<void(int)> onChange;
    void setSelected(int index); // no onChange
    [[nodiscard]] int selected() const noexcept { return m_selected; }
    // D-111: the titles again (counts in them, e.g. "Talep Üzerine Özellikler 57/489"); same count.
    // New titles alone (a count, often set by the very onChange of a tab click) keep the underline
    // sliding; other tabs altogether stop it.
    void setTabs(std::vector<std::wstring> tabs) {
        if (tabs.size() != m_tabs.size()) {
            m_move.stop();
        }
        m_tabs = std::move(tabs);
        m_selected = std::clamp(m_selected, 0, std::max(static_cast<int>(m_tabs.size()) - 1, 0));
        updateExtent();
        invalidate();
    }
    // D-091: a count after each tab's title (0 = none), e.g. the settings a tab changes.
    void setBadges(std::vector<int> badges);
    // The underline is on its way from another tab (the unit tests watch it end).
    [[nodiscard]] bool moving() const noexcept { return m_move.moving(); }
    // The colour the bar lies on (its edge fades blend into it); the page card's by default.
    void setSurface(tokens::Color surface) noexcept { m_surface = surface; }
    // The sideways scroll (tests, and where a tab is now drawn).
    [[nodiscard]] const StripScroll& strip() const noexcept { return m_strip; }
    [[nodiscard]] RectF tabRect(int index) const; // empty when out of range

    [[nodiscard]] SizeF measure(SizeF available) override;
    void layout() override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    bool onKeyDown(const KeyEvent& key) override;
    bool onWheel(PointF p, float lines) override;
    bool tick(double now) override;

private:
    [[nodiscard]] std::vector<RectF> tabRects() const;
    void tabRects(std::vector<RectF>& out) const; // into a kept buffer: paint() allocates nothing
    [[nodiscard]] float contentWidth() const;      // every tab and the gaps between them
    void updateExtent();                           // the strip's extent from the tabs and the bounds
    void revealSelected();
    [[nodiscard]] int badge(std::size_t tab) const;
    void select(int index); // a user's choice: the underline slides, onChange
    void hoverTo(int index);
    std::vector<std::wstring> m_tabs;
    std::vector<int> m_badges;
    std::vector<RectF> m_rects; // paint()'s
    int m_selected;
    int m_hover = -1;
    int m_hotChevron = 0; // the chevron under the pointer (-1 start, +1 end)
    ItemHover m_hoverFade;
    SelectionMove m_move;
    StripScroll m_strip;
    tokens::Color m_surface = tokens::Color::BgPanel;
};

class RadioGroup : public Widget {
public:
    RadioGroup(std::vector<std::wstring> options, int selected = 0);
    std::function<void(int)> onChange;
    void setSelected(int index); // no onChange
    [[nodiscard]] int selected() const noexcept { return m_selected; }

    [[nodiscard]] bool moving() const noexcept { return m_move.moving(); }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    [[nodiscard]] float focusRadius() const override { return tokens::radius::r2; }
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    bool onKeyDown(const KeyEvent& key) override;
    bool tick(double now) override;

private:
    [[nodiscard]] std::vector<RectF> optionRects() const;
    void optionRects(std::vector<RectF>& out) const; // into a kept buffer: paint() allocates nothing
    void choose(int index);
    void hoverTo(int index);
    std::vector<std::wstring> m_options;
    std::vector<RectF> m_rects; // paint()'s
    int m_selected;
    int m_hover = -1;
    ItemHover m_hoverFade;
    SelectionMove m_move;
};

} // namespace wl::ui
