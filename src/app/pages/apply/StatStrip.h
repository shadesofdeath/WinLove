#pragma once
// Counter row of screens 13 and 15: equal columns, each with a 1px line.subtle top rule,
// caption label (text.secondary) and a 16px value, optionally followed by a mono detail
// ("14  2,41 GB", "2,38 GB · %49").
#include "ui/widget/Widget.h"

#include <string>
#include <vector>

namespace wl::app {

class StatStrip : public ui::Widget {
public:
    static constexpr float kHeight = 48.0f;

    struct Stat {
        std::wstring label;
        std::wstring value;
        std::wstring detail;
        ui::tokens::Color valueInk = ui::tokens::Color::TextPrimary;
        ui::tokens::Color detailInk = ui::tokens::Color::TextSecondary;
    };

    void setStats(std::vector<Stat> stats);
    [[nodiscard]] ui::SizeF measure(ui::SizeF available) override;
    void paint(ui::Canvas& canvas) override;

private:
    std::vector<Stat> m_stats;
};

} // namespace wl::app
