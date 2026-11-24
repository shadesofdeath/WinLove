#pragma once
// Checkbox: since 2026-11-09 the round check of the Components page (a ring with a tick / dash,
// no fill) everywhere, at form size. Tables paint it inline with paintBox().
// A box just ticked draws its tick as a pen would (the dash grows from the middle) in
// motion.base; at rest, and under reduced motion, it is simply there.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <string>

namespace wl::ui {

enum class CheckState : std::uint8_t { Off, On, Indeterminate };

class Checkbox {
public:
    static constexpr float kBox = tokens::size::checkbox; // D-107: 18, radius 5
    // How long a tick takes to draw itself in (CheckField, TableView::revealCheck).
    static constexpr float kRevealMs = tokens::motion::baseMs;
    // Draws the 12×12 box with its top-left at `at`. `reveal` 0..1: how much of the tick / dash
    // is drawn yet (1 = all of it, the box at rest). `dim`: a box that cannot be changed (a faint
    // ring, the tick in the tertiary ink, no hover).
    static void paintBox(Canvas& canvas, PointF at, CheckState state, bool hovered = false, float reveal = 1.0f,
                         bool dim = false);
};

// A checkbox with its label (whole row clickable, Space toggles).
class CheckField : public Widget {
public:
    explicit CheckField(std::wstring label, bool checked = false);
    std::function<void(bool)> onChange;
    [[nodiscard]] bool checked() const noexcept { return m_checked; }
    void setChecked(bool checked); // no onChange
    void setLabel(std::wstring label);
    // The tick is still drawing itself in (the unit tests watch it end).
    [[nodiscard]] bool revealing() const noexcept { return m_reveal.running(); }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onClick() override;
    bool onKeyDown(const KeyEvent& key) override;
    bool tick(double now) override;

private:
    std::wstring m_label;
    bool m_checked;
    Tween m_reveal{1.0f};
};

} // namespace wl::ui
