#pragma once
// Filter pills of the WinLoveUI package (D-107, Painter::Pill; LAYOUT.md "Bileşenler araç
// satırı"): 30 high, fully round, 6 apart. The chosen one is filled with text.primary and carries
// SmallSemi in the page color; the others are outlined (line) with Small text.secondary
// (text.primary on hover). One of them is always chosen; ←/→ move it. A new choice's fill fades
// in over the old one's; hover fades like a Button's (ItemHover).
#include "ui/anim/ItemMotion.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::ui {

class PillGroup : public Widget {
public:
    explicit PillGroup(std::vector<std::wstring> options, int selected = 0);
    std::function<void(int)> onChange;
    void setSelected(int index); // no onChange
    // D-149: an option that cannot be chosen now (drawn in the disabled ink; clicks and arrows pass it by).
    void setOptionEnabled(int index, bool enabled);
    [[nodiscard]] bool optionEnabled(int index) const noexcept;
    [[nodiscard]] int selected() const noexcept { return m_selected; }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override {
        return m_hover >= 0 && optionEnabled(m_hover) ? Cursor::Hand : Cursor::Arrow;
    }
    [[nodiscard]] RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override { return tokens::size::pill / 2; }
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onPointerDown(PointF p) override;
    bool onKeyDown(const KeyEvent& key) override;
    bool tick(double now) override;
    [[nodiscard]] bool moving() const noexcept { return m_move.moving(); }

private:
    [[nodiscard]] std::vector<RectF> pillRects() const;
    void pillRects(std::vector<RectF>& out) const; // into a kept buffer: paint() allocates nothing
    void choose(int index);
    void hoverTo(int index);
    std::vector<std::wstring> m_options;
    std::vector<bool> m_disabled; // per option (D-149)
    std::vector<RectF> m_rects; // paint()'s
    int m_selected;
    int m_hover = -1;
    ItemHover m_hoverFade;
    SelectionMove m_move;
};

} // namespace wl::ui
