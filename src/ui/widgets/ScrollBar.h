#pragma once
// Vertical overlay ScrollBar per 03_components/progress-skeleton-empty.md: no arrows, 2px from
// the edge; thumb 4px at rest (line.strong, r1), 8px on hover/drag (text.tertiary), 140 ms.
// The owner keeps the offset: setRange() + setOffset(); dragging/clicking reports onScroll.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>

namespace wl::ui {

class ScrollBar : public Widget {
public:
    static constexpr float kWidth = 12.0f; // hit area; the thumb is thinner

    ScrollBar();

    std::function<void(float offset)> onScroll;

    void setRange(float content, float viewport);
    void setOffset(float offset);
    [[nodiscard]] float offset() const noexcept { return m_offset; }
    [[nodiscard]] float maxOffset() const noexcept;
    [[nodiscard]] bool needed() const noexcept { return m_content > m_viewport + 0.5f; }

    void paint(Canvas& canvas) override;
    [[nodiscard]] Widget* hitTest(PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onPressedChanged(bool pressed) override;
    void onPointerDown(PointF p) override;
    void onPointerMove(PointF p) override;
    bool tick(double now) override;

private:
    [[nodiscard]] RectF thumbRect() const;
    void scrollTo(float offset);

    float m_content = 0;
    float m_viewport = 0;
    float m_offset = 0;
    float m_grab = -1; // pointer offset inside the thumb while dragging
    Tween m_wide;
};

} // namespace wl::ui
