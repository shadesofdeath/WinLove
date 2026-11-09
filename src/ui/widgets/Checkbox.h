#pragma once
// Checkbox: since 2026-11-09 the round check of the Components page (a ring with a tick / dash,
// no fill) everywhere, at form size. Tables paint it inline with paintBox().
#include "ui/widget/Widget.h"

#include <functional>
#include <string>

namespace wl::ui {

enum class CheckState : std::uint8_t { Off, On, Indeterminate };

class Checkbox {
public:
    static constexpr float kBox = tokens::size::checkbox; // D-107: 18, radius 5
    // Draws the 12×12 box with its top-left at `at`.
    static void paintBox(Canvas& canvas, PointF at, CheckState state, bool hovered = false);
};

// A checkbox with its label (whole row clickable, Space toggles).
class CheckField : public Widget {
public:
    explicit CheckField(std::wstring label, bool checked = false);
    std::function<void(bool)> onChange;
    [[nodiscard]] bool checked() const noexcept { return m_checked; }
    void setChecked(bool checked); // no onChange
    void setLabel(std::wstring label);

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] Cursor cursor() const override { return Cursor::Hand; }
    void onClick() override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    std::wstring m_label;
    bool m_checked;
};

} // namespace wl::ui
