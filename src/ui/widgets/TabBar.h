#pragma once
// TabBar and RadioGroup per the WinLoveUI package (D-107).
// TabBar (Painter::Tab): text tabs 18 apart; selected = BodySemi text.primary + 2px accent
// underline; others BodyMedium text.secondary; counts in accent text; 1px line under the bar.
// ←/→ move, click selects.
// RadioGroup (Painter::Segmented): one of a few choices as a 34px segmented control — sunk track,
// the chosen segment on the panel color with a line border. ←/→ move the selection.
#include "ui/widget/Widget.h"

#include <algorithm>

#include <functional>
#include <string>
#include <vector>

namespace wl::ui {

class TabBar : public Widget {
public:
    explicit TabBar(std::vector<std::wstring> tabs, int selected = 0);
    std::function<void(int)> onChange;
    void setSelected(int index); // no onChange
    [[nodiscard]] int selected() const noexcept { return m_selected; }
    // D-111: the titles again (counts in them, e.g. "Talep Üzerine Özellikler 57/489"); same count.
    void setTabs(std::vector<std::wstring> tabs) {
        m_tabs = std::move(tabs);
        m_selected = std::clamp(m_selected, 0, std::max(static_cast<int>(m_tabs.size()) - 1, 0));
        invalidate();
    }
    // D-091: a count after each tab's title (0 = none), e.g. the settings a tab changes.
    void setBadges(std::vector<int> badges);

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] std::vector<RectF> tabRects() const;
    [[nodiscard]] int badge(std::size_t tab) const;
    std::vector<std::wstring> m_tabs;
    std::vector<int> m_badges;
    int m_selected;
    int m_hover = -1;
};

class RadioGroup : public Widget {
public:
    RadioGroup(std::vector<std::wstring> options, int selected = 0);
    std::function<void(int)> onChange;
    void setSelected(int index); // no onChange
    [[nodiscard]] int selected() const noexcept { return m_selected; }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    [[nodiscard]] float focusRadius() const override { return tokens::radius::r2; }
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] std::vector<RectF> optionRects() const;
    void choose(int index);
    std::vector<std::wstring> m_options;
    int m_selected;
    int m_hover = -1;
};

} // namespace wl::ui
