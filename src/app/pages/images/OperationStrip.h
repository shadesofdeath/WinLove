#pragma once
// Progress strip for a running engine operation (screen 03): 88px, bg.panel; spinner 16,
// title (bodyStrong), detail line (caption: path · step · % · ETA), 2px progress bar, percent on
// the right, "İptal" (secondary). Reads AppState::operation() each frame it is shown.
#include "app/Localization.h"
#include "app/state/AppState.h"
#include "ui/widgets/Button.h"

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
    void start(); // begin the spinner animation

private:
    [[nodiscard]] std::wstring eta(const EngineOperation& op, double now) const;

    const Localization& m_strings;
    const AppState& m_state;
    ui::Button* m_cancel = nullptr;
    double m_now = 0;
};

} // namespace wl::app
