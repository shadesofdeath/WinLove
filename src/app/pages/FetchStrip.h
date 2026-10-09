#pragma once
// A download / lookup in progress where a page's drop zone or list starts: spinner, what is
// happening (bodyStrong), a 2px bar with the percentage when the size is known, and "Durdur".
// The page says what to show through `status`; nullopt hides nothing by itself — the page decides
// visibility, the strip only paints. Animates while visible and `status` gives something.
#include "app/Localization.h"
#include "ui/widgets/Button.h"

#include <functional>
#include <optional>
#include <string>

namespace wl::app {

class FetchStrip : public ui::Widget {
public:
    struct Status {
        std::wstring title;
        double fraction = -1; // < 0: no bar
    };
    static constexpr float kHeight = 52.0f;

    FetchStrip(const Localization& strings, std::function<std::optional<Status>()> status);
    std::function<void()> onStop;

    void start(); // call when it becomes visible
    bool tick(double now) override;
    [[nodiscard]] float tickIntervalMs() const override { return 100.0f; } // spinner and ETA: 10 frames a second
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    std::function<std::optional<Status>()> m_status;
    ui::Button* m_stop = nullptr;
    double m_now = 0;
};

} // namespace wl::app
