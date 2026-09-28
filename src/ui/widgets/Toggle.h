#pragma once
// ToggleSwitch per 03_components/checkbox.md: 24×12, r1, knob 8×8; off: 1px line.strong + knob
// text.secondary at x=1; on: accent.base + knob text.onAccent at x=13; 140 ms. Label to the
// right (toolbar variant), 8px gap. Click / Space toggles.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <string>

namespace wl::ui {

class Toggle : public Widget {
public:
    explicit Toggle(std::wstring label, bool on = false);

    std::function<void(bool)> onChange;

    [[nodiscard]] bool isOn() const noexcept { return m_on; }
    // The 24×12 switch alone (table cells): `t` 0 = off … 1 = on.
    static void paintSwitch(Canvas& canvas, RectF track, float t, bool hovered);
    void setOn(bool on, bool animated = true); // no onChange

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] RectF focusRect() const override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onClick() override;
    bool onKeyDown(const KeyEvent& key) override;
    bool tick(double now) override;

private:
    std::wstring m_label;
    bool m_on;
    Tween m_knob;
};

} // namespace wl::ui
