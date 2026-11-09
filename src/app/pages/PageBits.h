#pragma once
// Small painting pieces shared by the table pages (risk dot + word, a detail line under a table).
#include "app/Localization.h"
#include "core/ops/ChangeSet.h"
#include "ui/render/Canvas.h"
#include "ui/widgets/TableView.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

// The architecture filter of the Images and Drivers pages: item i of archFilterItems() keeps
// what core::architectureName() calls kArchFilterKeys[i]; 0 = all.
inline constexpr const wchar_t* kArchFilterKeys[] = {L"", L"x64", L"arm64", L"x86"};
[[nodiscard]] inline std::vector<std::wstring> archFilterItems(const Localization& strings) {
    return {strings.get(Str::CommonAll), kArchFilterKeys[1], kArchFilterKeys[2], kArchFilterKeys[3]};
}

// D-110: the reference tool's tree check — a circle with a tick (on / kept), empty (off) or a dash (mixed); a
// template color as a crescent on its left (a disc of `arc` 2 to the left under a disc of the row's
// own background). `dim`: a row nothing can change (grey).
enum class TreeCheck : std::uint8_t { Off, Mixed, On };
inline void paintTreeCheck(ui::Canvas& canvas, ui::PointF c, TreeCheck state, std::optional<ui::tokens::Color> arc, bool dim,
                           const ui::TableView::CellState& cell) {
    using ui::tokens::Color;
    constexpr float r = 6.0f;
    if (arc) {
        canvas.fillEllipse({c.x - 2.0f, c.y}, r, *arc);
        canvas.fillEllipse(c, r, cell.selected ? Color::AccentSubtle : Color::BgPanel);
        if (cell.hovered && !cell.selected) {
            canvas.fillEllipse(c, r, Color::BgHover);
        }
    }
    canvas.strokeRoundRect({c.x - r, c.y - r, 2 * r, 2 * r}, r, dim ? Color::LineStrong : Color::TextTertiary);
    const ui::Ink ink = dim ? Color::TextTertiary : Color::TextSecondary;
    const float w = std::max(1.0f, 1.4f * canvas.scale());
    if (state == TreeCheck::On) {
        canvas.line({c.x - 3.0f, c.y + 0.2f}, {c.x - 0.8f, c.y + 2.4f}, ink, w);
        canvas.line({c.x - 0.8f, c.y + 2.4f}, {c.x + 3.2f, c.y - 2.3f}, ink, w);
    } else if (state == TreeCheck::Mixed) {
        canvas.line({c.x - 2.8f, c.y}, {c.x + 2.8f, c.y}, ink, w);
    }
}

// A risk's color (dot, warning icon) and word.
[[nodiscard]] inline ui::tokens::Color riskInk(core::ops::Risk risk) noexcept {
    using ui::tokens::Color;
    return risk == core::ops::Risk::Low ? Color::StatusSuccess : risk == core::ops::Risk::High ? Color::StatusError : Color::StatusWarning;
}
[[nodiscard]] inline Str riskText(core::ops::Risk risk) noexcept {
    return risk == core::ops::Risk::Low ? Str::RiskLow : risk == core::ops::Risk::High ? Str::RiskHigh : Str::RiskMedium;
}

// The risk cell of a table (D-107, the package's lists): nothing for a safe item; "● Dikkat" /
// "● Riskli" — a 6px dot and the word in the status color.
inline void paintRisk(ui::Canvas& canvas, ui::RectF rect, core::ops::Risk risk, const Localization& strings) {
    if (risk == core::ops::Risk::Low) {
        return;
    }
    const auto ink = riskInk(risk);
    canvas.fillEllipse({rect.x + 3, rect.y + rect.height / 2}, 3.0f, ink);
    canvas.drawText(strings.get(riskText(risk)), {rect.x + 13, rect.y, rect.width - 13, rect.height}, ui::tokens::TypeStyle::Small,
                    ink);
}

// A form section (ISO / USB form, Languages): title, a hairline under it, `area.height` tall.
inline void paintFormSection(ui::Canvas& canvas, ui::RectF area, const std::wstring& title) {
    canvas.drawText(title, {area.x, area.y + area.height - 26, std::min(area.width, 400.0f), 16}, ui::tokens::TypeStyle::Section,
                    ui::tokens::Color::TextTertiary);
}

// The caption a table shows under its header when it has no rows ("Sonuç yok", "Liste boş"):
// centered over `area` (x / width), 12 px under the header of a table starting at area.y.
inline void paintTableEmpty(ui::Canvas& canvas, ui::RectF area, const std::wstring& text) {
    canvas.drawText(text, {area.x, area.y + ui::TableView::kHeader + 12, area.width, 20}, ui::tokens::TypeStyle::Body,
                    ui::tokens::Color::TextTertiary, ui::TextAlign::Center);
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
