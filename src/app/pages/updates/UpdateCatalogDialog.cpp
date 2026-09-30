#include "app/pages/updates/UpdateCatalogDialog.h"

#include "app/Format.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/TableView.h"

#include <algorithm>
#include <format>

namespace wl::app {

using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {

constexpr float kWidth = 720.0f;
constexpr int kVisibleRows = 4; // at most: LCU, its preview, .NET, its preview

enum Column : int { kName, kKb, kDate, kSize, kNote };

} // namespace

UpdateCatalogDialog makeUpdateCatalogDialog(const Localization& strings, Language language, const core::CatalogTarget& target,
                                      std::vector<core::CatalogOffer> offers, UpdateCatalogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    const std::wstring body = strings.format(
        Str::UpdatesCatalogBody, {{L"release", std::format(L"{} {}", target.windows, target.release)},
                                  {L"arch", target.architecture},
                                  {L"build", std::format(L"{}.{}", target.build, target.revision)}});
    auto dialog = std::make_unique<ui::Dialog>(s(Str::UpdatesCatalogTitle), body, ui::icons::Icon::Download,
                                               Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto rows = std::make_shared<std::vector<core::CatalogOffer>>(std::move(offers));
    auto checked = std::make_shared<std::vector<bool>>();
    for (const auto& o : *rows) {
        checked->push_back(o.recommended && !o.olderThanImage);
    }
    const Localization* text = &strings; // outlives every dialog

    auto* table = &raw->setContent<ui::TableView>(
        ui::TableView::kHeader + ui::TableView::kRow * kVisibleRows,
        std::vector<ui::TableColumn>{{s(Str::UpdatesColUpdate), 0},
                                     {s(Str::UpdatesKb), 96},
                                     {s(Str::UpdatesColDate), 92},
                                     {s(Str::CommonSize), 84, ui::TextAlign::Trailing},
                                     {s(Str::UpdatesColNote), 120}});
    table->setRowCount(static_cast<int>(rows->size()));
    table->setAccessible(ui::AccessRole::Group, s(Str::UpdatesCatalogTitle));

    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto update = [raw, rows, checked, primary, text, language] {
        std::size_t count = 0;
        std::uint64_t bytes = 0;
        for (std::size_t i = 0; i < rows->size(); ++i) {
            if ((*checked)[i]) {
                ++count;
                bytes += (*rows)[i].entry.size;
            }
        }
        if (*primary) {
            (*primary)->setText(text->format(Str::UpdatesDownloadN, {{L"n", std::to_wstring(count)},
                                                                     {L"size", formatBytes(bytes, language)}}));
            (*primary)->setEnabled(count > 0);
            raw->layout(); // the button's width follows its text
        }
    };
    auto toggle = [table, rows, checked, update](int row) {
        if (row < 0 || row >= static_cast<int>(rows->size()) || (*rows)[static_cast<std::size_t>(row)].olderThanImage) {
            return;
        }
        (*checked)[static_cast<std::size_t>(row)] = !(*checked)[static_cast<std::size_t>(row)];
        update();
        table->refresh();
    };
    table->onCellClick = [toggle](int row, int, ui::PointF) { toggle(row); };
    table->onKey = [table, toggle](const ui::KeyEvent& key) {
        if (key.virtualKey == VK_SPACE) {
            toggle(table->selected());
            return true;
        }
        return false;
    };
    table->paintCell = [rows, checked, text, language](ui::Canvas& canvas, int row, int column, ui::RectF rect,
                                                       ui::TableView::CellState cell) {
        if (row < 0 || row >= static_cast<int>(rows->size())) {
            return;
        }
        const auto index = static_cast<std::size_t>(row);
        const auto& offer = (*rows)[index];
        const auto& e = offer.entry;
        const bool blocked = offer.olderThanImage;
        const Color ink = blocked ? Color::TextTertiary : Color::TextPrimary;
        switch (column) {
        case kName: {
            if (blocked) {
                canvas.pushOpacity(ui::tokens::opacity::disabled);
            }
            ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                                   (*checked)[index] ? ui::CheckState::On : ui::CheckState::Off, cell.hovered && !blocked);
            if (blocked) {
                canvas.popOpacity();
            }
            const float x = rect.x + ui::Checkbox::kBox + 8;
            std::wstring name = text->get(e.kind == core::CatalogKind::DotNet ? Str::UpdatesKindDotnet : Str::UpdatesKindLcu);
            const std::wstring month = std::format(L"{:04}-{:02}", e.year, e.month);
            name = month + L" " + name;
            const float nameW = std::min(rect.right() - x, std::ceil(canvas.text().measure(name, TypeStyle::Body)));
            canvas.drawText(name, {x, rect.y, nameW, rect.height}, cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                            ink);
            if (e.build > 0) {
                const float bx = x + nameW + 8;
                canvas.drawText(std::format(L"{}.{}", e.build, e.revision),
                                {bx, rect.y, std::max(rect.right() - bx, 0.0f), rect.height}, TypeStyle::Mono,
                                Color::TextTertiary);
            }
            break;
        }
        case kKb: canvas.drawText(e.kb, rect, TypeStyle::Mono, ink); break;
        case kDate:
            canvas.drawText(std::format(L"{:04}-{:02}-{:02}", e.year, e.month, e.day), rect, TypeStyle::Mono,
                            Color::TextSecondary);
            break;
        case kSize:
            canvas.drawText(formatBytes(e.size, language), rect, TypeStyle::Body, Color::TextSecondary,
                            ui::TextAlign::Trailing);
            break;
        case kNote: {
            const Str note = blocked ? Str::UpdatesNoteImageNewer
                             : e.preview ? Str::UpdatesNotePreview
                                         : Str::UpdatesNoteRecommended;
            canvas.drawText(text->get(note), rect, TypeStyle::Caption,
                            blocked ? Color::TextTertiary : e.preview ? Color::StatusWarning : Color::StatusSuccess);
            break;
        }
        default: break;
        }
    };

    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    *primary = &raw->addButton(
        ui::ButtonKind::Primary, std::wstring(),
        [rows, checked, close = actions.close, done = actions.download] {
            std::vector<core::CatalogEntry> picked;
            for (std::size_t i = 0; i < checked->size(); ++i) {
                if ((*checked)[i]) {
                    picked.push_back((*rows)[i].entry);
                }
            }
            if (picked.empty()) {
                return;
            }
            close();
            done(std::move(picked));
        },
        /*primary=*/true);
    raw->onCancel = actions.close;
    update();
    return UpdateCatalogDialog{std::move(dialog), table};
}

} // namespace wl::app
