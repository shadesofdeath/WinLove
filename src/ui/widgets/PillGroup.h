#pragma once
// Filter pills of the WinLoveUI package (D-107, Painter::Pill; LAYOUT.md "Bileşenler araç
// satırı"): 30 high, fully round, 6 apart. The chosen one is filled with text.primary and carries
// SmallSemi in the page color; the others are outlined (line) with Small text.secondary
// (text.primary on hover). One of them is always chosen; ←/→ move it.
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
    [[nodiscard]] int selected() const noexcept { return m_selected; }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return m_hover >= 0 ? Cursor::Hand : Cursor::Arrow; }
    [[nodiscard]] RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override { return tokens::size::pill / 2; }
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onPointerDown(PointF p) override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] std::vector<RectF> pillRects() const;
    void choose(int index);
    std::vector<std::wstring> m_options;
    int m_selected;
    int m_hover = -1;
};

} // namespace wl::ui
