#include "app/pages/components/CompatDialog.h"

#include "ui/widgets/Checkbox.h"
#include "ui/widgets/TableView.h"

#include <algorithm>

namespace wl::app {

namespace {

using ui::tokens::Color;
using ui::tokens::TypeStyle;

constexpr float kWidth = 880.0f;
constexpr int kVisibleRows = 13;
enum Column { kGuard, kWhat };

} // namespace

CompatDialog makeCompatDialog(const Localization& strings, Language language, const CompatCatalog& catalog,
                              const std::vector<std::wstring>& on, CompatDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::CompatTitle), s(Str::CompatBody), ui::icons::Icon::ShieldCheck,
                                               Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    // A copy: the dialog may outlive a change of the catalog's owner.
    auto rows = std::make_shared<std::vector<CompatCatalogEntry>>(catalog.guards());
    auto checked = std::make_shared<std::vector<bool>>();
    for (const auto& guard : *rows) {
        checked->push_back(std::ranges::find(on, guard.rule.id) != on.end());
    }

    auto* table = &raw->setContent<ui::TableView>(
        ui::TableView::kHeader + ui::TableView::kRow * std::min<float>(kVisibleRows, static_cast<float>(rows->size())),
        std::vector<ui::TableColumn>{{s(Str::CompatColumnGuard), 290}, {s(Str::CompatColumnWhat), 0}});
    table->setRowCount(static_cast<int>(rows->size()));
    table->setAccessible(ui::AccessRole::Group, s(Str::CompatTitle));

    auto toggle = [table, checked](int row) {
        if (row < 0 || row >= static_cast<int>(checked->size())) {
            return;
        }
        (*checked)[static_cast<std::size_t>(row)] = !(*checked)[static_cast<std::size_t>(row)];
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
    table->paintCell = [rows, checked, language](ui::Canvas& canvas, int row, int column, ui::RectF rect,
                                                 ui::TableView::CellState cell) {
        if (row < 0 || row >= static_cast<int>(rows->size())) {
            return;
        }
        const auto index = static_cast<std::size_t>(row);
        const auto& guard = (*rows)[index];
        switch (column) {
        case kGuard: {
            ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                                   (*checked)[index] ? ui::CheckState::On : ui::CheckState::Off, cell.hovered);
            const float x = rect.x + ui::Checkbox::kBox + 8;
            canvas.drawText(guard.name(language), {x, rect.y, rect.right() - x, rect.height},
                            cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
            break;
        }
        case kWhat: canvas.drawText(guard.description(language), rect, TypeStyle::Caption, Color::TextSecondary); break;
        default: break;
        }
    };

    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    raw->addButton(
        ui::ButtonKind::Primary, s(Str::CommonSave),
        [rows, checked, close = actions.close, save = actions.save] {
            std::vector<std::wstring> ids;
            for (std::size_t i = 0; i < rows->size(); ++i) {
                if ((*checked)[i]) {
                    ids.push_back((*rows)[i].rule.id);
                }
            }
            close();
            save(std::move(ids));
        },
        /*primary=*/true);
    raw->onCancel = actions.close;
    return CompatDialog{std::move(dialog), table};
}

} // namespace wl::app
