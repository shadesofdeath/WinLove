#pragma once
// Small painting pieces shared by the table pages (risk dot + word, a detail line under a table).
#include "app/Localization.h"
#include "core/ops/ChangeSet.h"
#include "ui/render/Canvas.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace wl::app {

inline void paintRisk(ui::Canvas& canvas, ui::RectF rect, core::ops::Risk risk, const Localization& strings) {
    using ui::tokens::Color;
    const Color ink = risk == core::ops::Risk::Low    ? Color::StatusSuccess
                      : risk == core::ops::Risk::High ? Color::StatusError
                                                      : Color::StatusWarning;
    canvas.fillRect({rect.x, rect.y + (rect.height - 6) / 2, 6, 6}, ink);
    const Str text = risk == core::ops::Risk::Low    ? Str::RiskLow
                     : risk == core::ops::Risk::High ? Str::RiskHigh
                                                     : Str::RiskMedium;
    canvas.drawText(strings.get(text), {rect.x + 12, rect.y, rect.width - 12, rect.height}, ui::tokens::TypeStyle::Caption,
                    Color::TextSecondary);
}

// The line under a table that says more about the selected row: a mono first part (a path),
// then the explanation.
inline constexpr float kDetailLine = 28.0f;
inline void paintDetail(ui::Canvas& canvas, ui::RectF rect, const std::wstring& mono, const std::wstring& text) {
    using ui::tokens::Color;
    using ui::tokens::TypeStyle;
    canvas.hairlineH(rect.x, rect.y, rect.width, Color::LineSubtle);
    float x = rect.x;
    if (!mono.empty()) {
        const float w = std::min(std::ceil(canvas.text().measure(mono, TypeStyle::Mono)), rect.width * 0.6f);
        canvas.drawText(mono, {x, rect.y + 1, w, rect.height - 1}, TypeStyle::Mono, Color::TextSecondary);
        x += w + 12;
    }
    canvas.drawText(text, {x, rect.y + 1, std::max(rect.right() - x, 0.0f), rect.height - 1}, TypeStyle::Caption,
                    Color::TextTertiary);
}

} // namespace wl::app
