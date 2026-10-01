#pragma once
// TabBar (screen 16) and RadioGroup (checkbox.md "RadioButton").
// TabBar: 24px tabs, 12px text; selected = bg.raised + bodyStrong text.primary + 2px accent underline;
// others text.secondary; 1px line.subtle under the whole bar. ←/→ move, click selects.
// RadioGroup: horizontal, 12px circle (1px line.strong; selected 1px accent + 6px accent dot),
// label 8px right, options 16px apart. ←/→ move the selection.
#include "ui/widget/Widget.h"

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

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] std::vector<RectF> tabRects() const;
    std::vector<std::wstring> m_tabs;
    int m_selected;
    int m_hover = -1;
};

class RadioGroup : public Widget {
public:
    RadioGroup(std::vector<std::wstring> options, int selected = 0);
    std::function<void(int)> onChange;
    void setSelected(int index); // no onChange
    [[nodiscard]] int selected() const noexcept { return m_selected; }
    void setOptionEnabled(int index, bool enabled);

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onPointerDown(PointF p) override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] std::vector<RectF> optionRects() const;
    void choose(int index);
    std::vector<std::wstring> m_options;
    std::vector<bool> m_enabled;
    int m_selected;
};

} // namespace wl::ui
