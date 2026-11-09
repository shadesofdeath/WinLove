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

// D-121: a row is a section title ("Windows", "Uygulamalar") or a guard.
struct Row {
    std::wstring title; // a section title when `guard` is negative
    int guard = -1;     // index into the catalog's guards
};

} // namespace

CompatDialog makeCompatDialog(const Localization& strings, Language language, const CompatCatalog& catalog,
                              const std::vector<std::wstring>& on, CompatDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::CompatTitle), s(Str::CompatBody), ui::icons::Icon::ShieldCheck,
                                               Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    // A copy: the dialog may outlive a change of the catalog's owner.
    auto guards = std::make_shared<std::vector<CompatCatalogEntry>>(catalog.guards());
    auto checked = std::make_shared<std::vector<bool>>();
    for (const auto& guard : *guards) {
        checked->push_back(guard.rule.always || std::ranges::find(on, guard.rule.id) != on.end());
    }
    // Windows' own features first, then what apps need, each under its title.
    auto rows = std::make_shared<std::vector<Row>>();
    for (const auto section : {CompatSection::Windows, CompatSection::Apps}) {
        bool titled = false;
        for (std::size_t i = 0; i < guards->size(); ++i) {
            if ((*guards)[i].section != section) {
                continue;
            }
            if (!titled) {
                rows->push_back({s(section == CompatSection::Apps ? Str::CompatSectionApps : Str::CompatSectionWindows)});
                titled = true;
            }
            rows->push_back({{}, static_cast<int>(i)});
        }
    }

    auto* table = &raw->setContent<ui::TableView>(
        ui::TableView::kHeader + ui::TableView::kRow * std::min<float>(kVisibleRows, static_cast<float>(rows->size())),
        std::vector<ui::TableColumn>{{s(Str::CompatColumnGuard), 290}, {s(Str::CompatColumnWhat), 0}});
    table->setRowCount(static_cast<int>(rows->size()));
    table->setAccessible(ui::AccessRole::Group, s(Str::CompatTitle));

    // A guard that is always on ("core") and the section titles do not toggle.
    auto toggle = [table, rows, guards, checked](int row) {
        if (row < 0 || row >= static_cast<int>(rows->size())) {
            return;
        }
        const int guard = (*rows)[static_cast<std::size_t>(row)].guard;
        if (guard < 0 || (*guards)[static_cast<std::size_t>(guard)].rule.always) {
            return;
        }
        (*checked)[static_cast<std::size_t>(guard)] = !(*checked)[static_cast<std::size_t>(guard)];
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
    table->paintCell = [rows, guards, checked, language](ui::Canvas& canvas, int row, int column, ui::RectF rect,
                                                         ui::TableView::CellState cell) {
        if (row < 0 || row >= static_cast<int>(rows->size())) {
            return;
        }
        const Row& line = (*rows)[static_cast<std::size_t>(row)];
        if (line.guard < 0) {
            if (column == kGuard) {
                canvas.drawText(line.title, rect, TypeStyle::BodyStrong, Color::TextSecondary);
            }
            return;
        }
        const auto index = static_cast<std::size_t>(line.guard);
        const auto& guard = (*guards)[index];
        switch (column) {
        case kGuard: {
            const ui::PointF box{rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2};
            if (guard.rule.always) {
                canvas.drawIcon(ui::icons::Icon::Lock, {box.x, rect.y + (rect.height - ui::tokens::size::icon) / 2},
                                Color::TextSecondary);
            } else {
                ui::Checkbox::paintBox(canvas, box, (*checked)[index] ? ui::CheckState::On : ui::CheckState::Off,
                                       cell.hovered);
            }
            const float x = rect.x + ui::Checkbox::kBox + 8;
            canvas.drawText(guard.name(language), {x, rect.y, rect.right() - x, rect.height},
                            cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body,
                            guard.rule.always ? Color::TextSecondary : Color::TextPrimary);
            break;
        }
        case kWhat: canvas.drawText(guard.description(language), rect, TypeStyle::Caption, Color::TextSecondary); break;
        default: break;
        }
    };

    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    raw->addButton(
        ui::ButtonKind::Primary, s(Str::CommonSave),
        [guards, checked, close = actions.close, save = actions.save] {
            std::vector<std::wstring> ids;
            for (std::size_t i = 0; i < guards->size(); ++i) {
                if ((*checked)[i] && !(*guards)[i].rule.always) {
                    ids.push_back((*guards)[i].rule.id);
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
