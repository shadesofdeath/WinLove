#pragma once
// Progress strip for a running engine operation (screen 03): 88px, bg.panel; progress ring 20 (D-142),
// title (bodyStrong), detail line (caption: path · step · % · ETA), 2px progress bar, percent on
// the right, "İptal" (secondary). Reads AppState::operation() each frame it is shown.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "ui/widgets/Button.h"
#include "ui/widgets/ProgressRing.h"

#include <functional>

namespace wl::app {

class OperationStrip : public ui::Widget {
public:
    static constexpr float kHeight = 88.0f;

    OperationStrip(const Localization& strings, const AppState& state);
    std::function<void()> onCancel;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool tick(double now) override;
    // The ring's pace while it turns (D-142), else the ETA's: 10 frames a second.
    [[nodiscard]] float tickIntervalMs() const override { return m_ring.tickMs(100.0f); }
    void start(); // begin the ring's animation

private:
    [[nodiscard]] std::wstring eta(const EngineOperation& op, double now) const;

    const Localization& m_strings;
    const AppState& m_state;
    ui::Button* m_cancel = nullptr;
    double m_now = 0;
    ui::RingClock m_ring;
};

} // namespace wl::app
