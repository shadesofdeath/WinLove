#pragma once
// Small painting pieces shared by the table pages (risk dot + word, a detail line under a table).
#include "app/Localization.h"
#include "core/ops/ChangeSet.h"
#include "ui/render/Canvas.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/TableView.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// D-136: what a table's flexible (name) column keeps at a narrow window: the fixed columns that may
// shrink (TableColumn::minWidth) give it this much before the names are cut.
inline constexpr float kNameColumnMin = 260.0f;

// D-110: the reference tool's tree check — a circle with a tick (on / kept), empty (off) or a dash (mixed); a
// template color as a crescent on its left (a disc of `arc` 2 to the left under a disc of the row's
// own background). `dim`: a row nothing can change (grey). A row the user just ticked draws its
// tick in (cell.checkReveal, TableView::revealCheck), as Checkbox::paintBox does.
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
    const float reveal = std::clamp(cell.checkReveal, 0.0f, 1.0f);
    if (state == TreeCheck::On && reveal > 0.0f) {
        const ui::PointF start{c.x - 3.0f, c.y + 0.2f};
        const ui::PointF corner{c.x - 0.8f, c.y + 2.4f};
        const ui::PointF end{c.x + 3.2f, c.y - 2.3f};
        // Drawn in: the short stroke down, then the long one up, at one pace.
        const float first = std::hypot(corner.x - start.x, corner.y - start.y);
        const float second = std::hypot(end.x - corner.x, end.y - corner.y);
        const float drawn = reveal * (first + second);
        const float a = std::min(drawn / first, 1.0f);
        canvas.line(start, {start.x + (corner.x - start.x) * a, start.y + (corner.y - start.y) * a}, ink, w);
        if (drawn > first) {
            const float b = std::min((drawn - first) / second, 1.0f);
            canvas.line(corner, {corner.x + (end.x - corner.x) * b, corner.y + (end.y - corner.y) * b}, ink, w);
        }
    } else if (state == TreeCheck::Mixed && reveal > 0.0f) {
        const float half = 2.8f * reveal; // grows out from the middle
        canvas.line({c.x - half, c.y}, {c.x + half, c.y}, ink, w);
    }
}

// A table's column heads drawn in the page's filter row (D-110, D-111): the box a column's head
// takes, at the very x / width its cells get (TableView pads every cell by kCellPad), so a head
// lines up with what is under it. Called from layout() and kept; never from paint().
[[nodiscard]] inline ui::RectF columnHead(const ui::TableView& table, int column, float y, float height) {
    const ui::RectF cell = table.cellRect(0, column);
    return {cell.x + ui::TableView::kCellPad, y, std::max(cell.width - 2 * ui::TableView::kCellPad, 0.0f), height};
}

// What a tree's category / node row says about what is under it (Bileşenler, Özellikler): how
// many of its items are ticked ("12/40", common.ofTotal) and their total size. Worked out when
// the rows or the queue change (never in paint); painted quietly, so an item row next to it
// does not move. `countRoom`: how much of the name column the count may take, from the right.
struct TreeSummary {
    std::wstring count; // "12/40"; empty: nothing to say
    std::wstring size;  // the page's own size format; empty: none
};
inline constexpr float kTreeCountRoom = 64.0f;
[[nodiscard]] inline std::wstring treeCount(const Localization& strings, std::size_t on, std::size_t all) {
    return all == 0 ? std::wstring() : strings.format(Str::CommonOfTotal, {{L"on", std::to_wstring(on)}, {L"n", std::to_wstring(all)}});
}
// The count at the right of a category / node row's name cell (the name is clipped before it).
inline void paintTreeCount(ui::Canvas& canvas, ui::RectF nameCell, const std::wstring& count) {
    if (count.empty()) {
        return;
    }
    canvas.drawText(count, {nameCell.right() - kTreeCountRoom, nameCell.y, kTreeCountRoom, nameCell.height}, ui::tokens::TypeStyle::Body,
                    ui::tokens::Color::TextTertiary, ui::TextAlign::Trailing);
}

// A risk's color (dot, warning icon) and word (D-139: the marks below use them too).
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

// A form section (ISO / USB form, Languages, Kaynak): its title over a title block `area` (usually
// FormView::kSection tall) — the very title every FormView draws ("Veri toplama", "Görünüm"; D-136:
// one helper, one style, its box the style's line height).
inline void paintFormSection(ui::Canvas& canvas, ui::RectF area, const std::wstring& title) {
    ui::FormView::paintSectionTitle(canvas, area, title);
}

// D-138: a size before → after as one bar (the image overview, Uygula's plan and result): the larger of
// the two is the whole width; what stays in the readable accent, what goes in green (smaller) or what
// comes in amber (larger). Nothing for an unknown size (before 0).
inline void paintSizeBar(ui::Canvas& canvas, ui::RectF rect, std::uint64_t before, std::uint64_t after) {
    using ui::tokens::Color;
    if (before == 0 || rect.width <= 0) {
        return;
    }
    const double most = static_cast<double>(std::max(before, after > 0 ? after : before));
    const float r = rect.height / 2;
    const float wBefore = std::max(rect.height, std::round(rect.width * static_cast<float>(static_cast<double>(before) / most)));
    const float wAfter =
        after > 0 ? std::max(rect.height, std::round(rect.width * static_cast<float>(static_cast<double>(after) / most))) : wBefore;
    canvas.fillRoundRect(rect, r, Color::SizeBar);
    if (after > 0 && after < before) {
        canvas.fillRoundRect({rect.x, rect.y, wBefore, rect.height}, r, ui::Ink(Color::StatusSuccess, Color::StatusSuccess, 0, 0.6f));
        canvas.fillRoundRect({rect.x, rect.y, wAfter, rect.height}, r, Color::AccentText);
    } else if (after > before) {
        canvas.fillRoundRect({rect.x, rect.y, wAfter, rect.height}, r, ui::Ink(Color::StatusWarning, Color::StatusWarning, 0, 0.6f));
        canvas.fillRoundRect({rect.x, rect.y, wBefore, rect.height}, r, Color::AccentText);
    }
    // The same size (2026-11-11, the owner: a full bar read as a gain): the neutral track alone, no
    // segment — "Boyutu değişmez" under it says the rest.
}

// D-138: a run's progress as a rounded meter (the 2 px progressBar's colours, thicker: it is the
// screen's subject while Uygula runs).
inline void paintMeter(ui::Canvas& canvas, ui::RectF rect, float fraction, ui::Ink fill = ui::tokens::Color::AccentBase) {
    const float r = rect.height / 2;
    canvas.fillRoundRect(rect, r, ui::tokens::Color::LineStrong);
    const float w = std::round(rect.width * std::clamp(fraction, 0.0f, 1.0f));
    if (w > 0) {
        canvas.fillRoundRect({rect.x, rect.y, std::max(w, rect.height), rect.height}, r, fill);
    }
}

// ---- D-139: one state vocabulary for every working list ---------------------------------------
// A row's state at a glance, in a narrow column of its own right after the name (never appended to
// the name): the change Uygula will make, or what keeps the row as it is; then its risk.
//   Queued  QueueClock in accent.text   — queued: Uygula changes it (the page says how)
//   Held    ShieldCheck in text.tertiary — a compatibility guard keeps it (Uyumluluk can lift it)
//   Locked  Lock in text.tertiary        — it cannot be changed here (the reason says why)
//   risk    a 6 px dot: status.warning (Dikkat) / status.error (Riskli); nothing for a safe row
//   recommended  PresetBookmark in text.tertiary — a template / the catalog recommends it
// Each mark has its slot, so the same mark lines up down the column; its meaning is the cell's
// tooltip (TableView::cellTooltip) and a fact of the detail card.
enum class RowMark : std::uint8_t { None, Queued, Held, Locked };
struct RowMarks {
    RowMark mark = RowMark::None;
    core::ops::Risk risk = core::ops::Risk::Low;
    bool recommended = false;
};
inline constexpr float kMarkSlot = ui::tokens::size::icon;      // a mark's square
inline constexpr float kMarkGap = ui::tokens::spacing::s2;       // between two slots
// The column: the cell's padding on both sides and the slots a page uses (state; + risk; + recommended).
[[nodiscard]] constexpr float marksColumnWidth(int slots) noexcept {
    return 2 * ui::TableView::kCellPad + static_cast<float>(slots) * kMarkSlot + static_cast<float>(slots > 0 ? slots - 1 : 0) * kMarkGap;
}
[[nodiscard]] inline ui::icons::Icon markIcon(RowMark mark) noexcept {
    return mark == RowMark::Queued ? ui::icons::Icon::QueueClock : mark == RowMark::Held ? ui::icons::Icon::ShieldCheck : ui::icons::Icon::Lock;
}
[[nodiscard]] inline ui::tokens::Color markInk(RowMark mark) noexcept {
    return mark == RowMark::Queued ? ui::tokens::Color::AccentText : ui::tokens::Color::TextTertiary;
}
// `slots`: 1 = state, 2 = state + risk, 3 = state + risk + recommended (the page's column width).
inline void paintMarks(ui::Canvas& canvas, ui::RectF cell, const RowMarks& marks, int slots) {
    const float cy = cell.y + cell.height / 2;
    float x = cell.x;
    if (marks.mark != RowMark::None) {
        canvas.drawIcon(markIcon(marks.mark), {x, std::round(cy - kMarkSlot / 2)}, markInk(marks.mark));
    }
    x += kMarkSlot + kMarkGap;
    if (slots >= 2 && marks.risk != core::ops::Risk::Low) {
        canvas.fillEllipse({x + kMarkSlot / 2, cy}, ui::tokens::size::dot / 2, riskInk(marks.risk));
    }
    x += kMarkSlot + kMarkGap;
    if (slots >= 3 && marks.recommended) {
        canvas.drawIcon(ui::icons::Icon::PresetBookmark, {x, std::round(cy - kMarkSlot / 2)}, ui::tokens::Color::TextTertiary);
    }
}
// A list whose every row is queued (Dosyalar: the list is the queue, a clock on each row would say
// nothing): its marks column has the risk's slot only (marksColumnWidth(1)), the dot as paintMarks draws it.
inline void paintRiskMark(ui::Canvas& canvas, ui::RectF cell, core::ops::Risk risk) {
    if (risk != core::ops::Risk::Low) {
        canvas.fillEllipse({cell.x + kMarkSlot / 2, cell.y + cell.height / 2}, ui::tokens::size::dot / 2, riskInk(risk));
    }
}
// What the marks of a row say, one line each, for the marks cell's tooltip: `change` is the page's
// own words for the queued change ("Uygula'da kaldırılacak"), `reason` why a row is held / locked.
[[nodiscard]] inline std::wstring marksTooltip(const Localization& strings, const RowMarks& marks, const std::wstring& change,
                                               const std::wstring& reason, const std::wstring& riskNote = {}) {
    std::wstring tip;
    auto line = [&](const std::wstring& text) {
        if (!text.empty()) {
            tip += (tip.empty() ? L"" : L"\n") + text;
        }
    };
    switch (marks.mark) {
    case RowMark::Queued: line(change.empty() ? strings.get(Str::MarksQueued) : change); break;
    case RowMark::Held: line(strings.get(Str::MarksHeld) + (reason.empty() ? L"" : L": " + reason)); break;
    case RowMark::Locked: line(strings.get(Str::MarksLocked) + (reason.empty() ? L"" : L": " + reason)); break;
    case RowMark::None: break;
    }
    if (marks.risk != core::ops::Risk::Low) {
        line(strings.get(Str::RiskColumn) + L": " + strings.get(riskText(marks.risk)) + (riskNote.empty() ? L"" : L" — " + riskNote));
    }
    if (marks.recommended) {
        line(strings.get(Str::MarksRecommended));
    }
    return tip;
}

// D-139: how deep each level of a tree is indented (Bileşenler, Özellikler, Sürücüler), and a group
// row's name: semibold, so a category reads as the head of what is under it.
inline constexpr float kTreeIndent = ui::tokens::spacing::s6;
inline constexpr ui::tokens::TypeStyle kGroupStyle = ui::tokens::TypeStyle::BodySemi;

// D-139: beside the detail card a table leaves out, in `order`, the columns the card repeats for the
// selected row until the rest fits `width` (its flexible column may narrow to kDetailFlexMin there:
// the card says the whole name); without the card every column is back, as before.
inline constexpr float kDetailFlexMin = 200.0f;
inline void fitColumns(ui::TableView& table, float width, bool detailShown, std::initializer_list<int> order,
                       float flexMin = kNameColumnMin) {
    table.setFlexMin(detailShown ? std::min(flexMin, kDetailFlexMin) : flexMin);
    for (const int column : order) {
        table.setColumnHidden(column, false);
    }
    if (!detailShown) {
        return;
    }
    for (const int column : order) {
        if (table.minimumWidth() <= width) {
            return;
        }
        table.setColumnHidden(column, true);
    }
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
