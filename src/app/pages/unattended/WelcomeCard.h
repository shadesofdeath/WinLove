#pragma once
// 2026-11-09 (the user): WinLove's own setup screens (D-084) at the top of Katılımsız — whether
// they are on can be seen at a glance (a large switch and an ON / OFF badge), and the wizard's
// stages are drawn in the app: a chip per stage in the order setup shows them (the ones the plan
// leaves out greyed), and the chosen stage as the wizard lays it out — its picture, the step bar,
// title, the controls it asks with, "Next" in the plan's accent. A click on the drawing goes to the
// next stage; the real window (oobe.ps1, preview mode) is one button away.
#include "app/Localization.h"
#include "core/unattend/Welcome.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/Toggle.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace wl::app {

class WelcomeCard : public ui::Widget {
public:
    explicit WelcomeCard(const Localization& strings);

    // As tall as its left column needs at `width` (the text wraps, the chips wrap); never under 230.
    [[nodiscard]] float preferredHeight(float width) const;

    std::function<void(bool on)> onToggle;      // the switch
    std::function<void()> onWindowPreview;      // "Gerçek pencerede önizle"
    void setState(bool on, const core::WelcomePlan& plan);

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    void onPointerMove(ui::PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onClick() override;
    [[nodiscard]] ui::Cursor cursor() const override;

    enum class Stage : std::uint8_t { Account, Network, Computer, Look, Wallpaper, Bundles, Prefs, Privacy, Finish, Count };

    struct Look; // the setup screens' colours in the plan's theme (D-126, WelcomeCard.cpp)

private:
    [[nodiscard]] bool asked(Stage stage) const;
    [[nodiscard]] Str stageName(Stage stage) const;
    [[nodiscard]] Str stageSub(Stage stage) const;
    [[nodiscard]] static float leftWidth(float cardWidth);
    [[nodiscard]] ui::RectF leftColumn() const;
    [[nodiscard]] float bodyHeight(float width) const; // the description, wrapped (the taller of on / off)
    // The chips wrapped from (x, top) in `width`, in setup's order.
    [[nodiscard]] std::vector<ui::RectF> chipRects(float x, float top, float width) const;
    void paintPreview(ui::Canvas& canvas, ui::RectF screen) const;
    void paintStageBody(ui::Canvas& canvas, ui::RectF area, Stage stage, const Look& look) const;

    const Localization& m_strings;
    ui::Toggle* m_toggle = nullptr;
    ui::Button* m_window = nullptr;
    bool m_on = false;
    core::WelcomePlan m_plan;
    int m_selected = 0;  // the stage drawn
    int m_hover = -1;    // a chip under the pointer; -2: the drawing
    std::vector<ui::RectF> m_chips; // one per Stage
    ui::RectF m_screen;             // the drawing
    float m_bodyTop = 0;
    float m_bodyH = 0;
    float m_chipsTop = 0;
};

} // namespace wl::app
