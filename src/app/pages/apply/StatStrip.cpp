#include "app/pages/apply/StatStrip.h"

#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kGap = 16.0f;
constexpr float kLabelTop = 8.0f;
constexpr float kValueTop = 24.0f;
} // namespace

void StatStrip::setStats(std::vector<Stat> stats) {
    m_stats = std::move(stats);
    setHitTestVisible(false);
    invalidate();
}

ui::SizeF StatStrip::measure(ui::SizeF available) {
    return {available.width, kHeight};
}

void StatStrip::paint(ui::Canvas& canvas) {
    if (m_stats.empty()) {
        return;
    }
    const RectF b = bounds();
    const auto n = static_cast<float>(m_stats.size());
    const float width = (b.width - kGap * (n - 1)) / n;
    for (std::size_t i = 0; i < m_stats.size(); ++i) {
        const auto& s = m_stats[i];
        const float x = std::round(b.x + static_cast<float>(i) * (width + kGap));
        canvas.hairlineH(x, b.y, width, Color::LineSubtle);
        canvas.drawText(s.label, {x, b.y + kLabelTop, width, 16}, TypeStyle::Caption, Color::TextSecondary);
        const float valueWidth = std::ceil(canvas.text().measure(s.value, TypeStyle::Title));
        canvas.drawText(s.value, {x, b.y + kValueTop - 2, valueWidth + 2, 22}, TypeStyle::Title, s.valueInk);
        if (!s.detail.empty()) {
            const float dx = x + valueWidth + 8;
            canvas.drawText(s.detail, {dx, b.y + kValueTop, std::max(x + width - dx, 0.0f), 20}, TypeStyle::Mono,
                            s.detailInk);
        }
    }
}

} // namespace wl::app
