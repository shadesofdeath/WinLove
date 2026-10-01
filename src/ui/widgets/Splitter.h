#pragma once
// Vertical splitter (progress-skeleton-empty.md): 1px line.subtle, 6px hit area centered on it,
// hover line.strong, dragging accent, double click resets. Reports drag deltas; the owner resizes.
#include "ui/widget/Widget.h"

#include <functional>

namespace wl::ui {

class Splitter : public Widget {
public:
    Splitter();

    std::function<void()> onDragStart;
    std::function<void(float totalDx)> onDrag; // relative to the drag start
    std::function<void()> onReset;             // double click

    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::SizeWE; }
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    void onPointerUp(PointF p) override;
    void onDoubleClick() override;
    void onPressedChanged(bool pressed) override;

private:
    float m_dragStartX = 0;
    bool m_dragging = false;
};

} // namespace wl::ui
