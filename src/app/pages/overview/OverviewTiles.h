#pragma once
// D-138: the clickable pieces of the image overview. A tile: one list of the image — its page's icon
// and name, the count large, a quiet line under it, and what the queue does there as the nav's
// count badge; a list not read yet says so in place of the count. A chip: a nav item of the queue
// ("Bileşenler 12"). Both open their page (Enter / Space too); hover and press like a button's
// (Tween, motion.fast). Texts are set from the state and measured in layout(), never in paint().
#include "ui/anim/Tween.h"
#include "ui/generated/Icons.g.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <string>

namespace wl::app {

// Hover / press / click / keys, as Button has them.
class OverviewLink : public ui::Widget {
public:
    OverviewLink();
    std::function<void()> onInvoke;

    void onHoverChanged(bool hovered) override;
    void onPressedChanged(bool pressed) override;
    void onClick() override;
    bool onKeyDown(const ui::KeyEvent& key) override;
    bool tick(double now) override;

protected:
    [[nodiscard]] float hot() const; // 0..1: hover, or press
    void paintHot(ui::Canvas& canvas, float radius) const;

private:
    ui::Tween m_hover;
    ui::Tween m_press;
};

class OverviewTile : public OverviewLink {
public:
    static constexpr float kHeight = 64.0f;
    struct Texts {
        ui::icons::Icon icon = ui::icons::Icon::LayersEditions;
        std::wstring label;  // "Bileşenler"
        std::wstring value;  // "1.284" — or the state of a list not read ("henüz okunmadı")
        std::wstring detail; // a quiet line after the value
        std::wstring badge;  // the queue's count there; empty: none
        bool quiet = false;  // `value` is a state word, not a number
    };
    void set(Texts texts);
    void layout() override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] float focusRadius() const override;

private:
    Texts m_texts;
    float m_valueWidth = 0;
    float m_badgeWidth = 0;
};

class OverviewChip : public OverviewLink {
public:
    static constexpr float kHeight = 26.0f;
    OverviewChip(std::wstring label, std::wstring count);
    [[nodiscard]] ui::SizeF measure(ui::SizeF available) override;
    void paint(ui::Canvas& canvas) override;
    [[nodiscard]] float focusRadius() const override;

private:
    std::wstring m_label;
    std::wstring m_count;
    float m_labelWidth = -1;
    float m_countWidth = -1;
};

} // namespace wl::app
