#pragma once
// Small painting pieces shared by the table pages (risk dot + word, a detail line under a table).
#include "app/Localization.h"
#include "core/ops/ChangeSet.h"
#include "ui/render/Canvas.h"
#include "ui/widgets/TableView.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace wl::app {

// The architecture filter of the Images and Drivers pages: item i of archFilterItems() keeps
// what core::architectureName() calls kArchFilterKeys[i]; 0 = all.
inline constexpr const wchar_t* kArchFilterKeys[] = {L"", L"x64", L"arm64", L"x86"};
[[nodiscard]] inline std::vector<std::wstring> archFilterItems(const Localization& strings) {
    return {strings.get(Str::CommonAll), kArchFilterKeys[1], kArchFilterKeys[2], kArchFilterKeys[3]};
}

// A risk's color (dot, warning icon) and word.
[[nodiscard]] inline ui::tokens::Color riskInk(core::ops::Risk risk) noexcept {
    using ui::tokens::Color;
    return risk == core::ops::Risk::Low ? Color::StatusSuccess : risk == core::ops::Risk::High ? Color::StatusError : Color::StatusWarning;
}
[[nodiscard]] inline Str riskText(core::ops::Risk risk) noexcept {
    return risk == core::ops::Risk::Low ? Str::RiskLow : risk == core::ops::Risk::High ? Str::RiskHigh : Str::RiskMedium;
}

// The risk cell of a table: a 6px dot and the word.
inline void paintRisk(ui::Canvas& canvas, ui::RectF rect, core::ops::Risk risk, const Localization& strings) {
    canvas.fillRect({rect.x, rect.y + (rect.height - 6) / 2, 6, 6}, riskInk(risk));
    canvas.drawText(strings.get(riskText(risk)), {rect.x + 12, rect.y, rect.width - 12, rect.height}, ui::tokens::TypeStyle::Caption,
                    ui::tokens::Color::TextSecondary);
}

// A form section (ISO / USB form, Languages): title, a hairline under it, `area.height` tall.
inline void paintFormSection(ui::Canvas& canvas, ui::RectF area, const std::wstring& title) {
    canvas.drawText(title, {area.x, area.y + 8, std::min(area.width, 400.0f), 20}, ui::tokens::TypeStyle::Section,
                    ui::tokens::Color::TextSecondary);
    canvas.hairlineH(area.x, area.y + area.height - 6, area.width, ui::tokens::Color::LineSubtle);
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
