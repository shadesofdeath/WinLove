#include "app/pages/postsetup/AppsDialog.h"

#include "app/controllers/PostSetupController.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/TableView.h"

#include <algorithm>

namespace wl::app {

using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {

constexpr float kWidth = 640.0f;
constexpr int kVisibleRows = 12;

enum Column : int { kName, kCategory, kId };

Str categoryName(PostSetupController::AppCategory category) {
    using Category = PostSetupController::AppCategory;
    switch (category) {
    case Category::Browsers: return Str::PostsetupCatBrowsers;
    case Category::Tools: return Str::PostsetupCatTools;
    case Category::Media: return Str::PostsetupCatMedia;
    case Category::Development: return Str::PostsetupCatDevelopment;
    case Category::Communication: return Str::PostsetupCatCommunication;
    case Category::Games: return Str::PostsetupCatGames;
    case Category::Office: return Str::PostsetupCatOffice;
    }
    return Str::PostsetupCatTools;
}

} // namespace

AppsDialog makeAppsDialog(const Localization& strings, std::wstring body, AppsDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::PostsetupCatalog), std::move(body), std::nullopt, Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    const auto* apps = &PostSetupController::popularApps(); // a static list
    const Localization* text = &strings;                    // outlives every dialog

    auto present = std::make_shared<std::vector<bool>>();
    for (std::size_t i = 0; i < apps->size(); ++i) {
        present->push_back(actions.present && actions.present(i));
    }
    auto checked = std::make_shared<std::vector<bool>>(apps->size(), false);

    auto* table = &raw->setContent<ui::TableView>(
        ui::TableView::kHeader + ui::TableView::kRow * kVisibleRows,
        std::vector<ui::TableColumn>{{s(Str::PostsetupCatalogApp), 0}, {s(Str::PostsetupCatalogCategory), 120}, {s(Str::PostsetupWingetId), 250}});
    table->setRowCount(static_cast<int>(apps->size()));
    table->setAccessible(ui::AccessRole::Group, s(Str::PostsetupCatalog));

    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto update = [raw, checked, primary, text] {
        const auto count = std::ranges::count(*checked, true);
        if (*primary) {
            (*primary)->setText(text->format(Str::PostsetupCatalogAdd, {{L"n", std::to_wstring(count)}}));
            (*primary)->setEnabled(count > 0);
            raw->layout(); // the button's width follows its text
        }
    };
    auto toggle = [table, present, checked, update](int row) {
        if (row < 0 || row >= static_cast<int>(checked->size()) || (*present)[static_cast<std::size_t>(row)]) {
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
    table->paintCell = [apps, present, checked, text](ui::Canvas& canvas, int row, int column, ui::RectF rect,
                                                      ui::TableView::CellState cell) {
        if (row < 0 || row >= static_cast<int>(apps->size())) {
            return;
        }
        const auto index = static_cast<std::size_t>(row);
        const auto& app = (*apps)[index];
        const bool has = (*present)[index];
        switch (column) {
        case kName: {
            if (has) {
                canvas.pushOpacity(ui::tokens::opacity::disabled);
            }
            ui::Checkbox::paintBox(canvas, {rect.x, rect.y + (rect.height - ui::Checkbox::kBox) / 2},
                                   has || (*checked)[index] ? ui::CheckState::On : ui::CheckState::Off, cell.hovered && !has);
            if (has) {
                canvas.popOpacity();
            }
            const float x = rect.x + ui::Checkbox::kBox + 8;
            canvas.drawText(app.name, {x, rect.y, std::max(rect.right() - x, 0.0f), rect.height},
                            cell.selected ? TypeStyle::BodyStrong : TypeStyle::Body, has ? Color::TextTertiary : Color::TextPrimary);
            break;
        }
        case kCategory:
            canvas.drawText(text->get(categoryName(app.category)), rect, TypeStyle::Caption, Color::TextSecondary);
            break;
        case kId:
            canvas.drawText(has ? app.id + L" · " + text->get(Str::PostsetupCatalogPresent) : app.id, rect, TypeStyle::Mono,
                            Color::TextTertiary);
            break;
        default: break;
        }
    };

    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    *primary = &raw->addButton(
        ui::ButtonKind::Primary, std::wstring(),
        [checked, close = actions.close, done = actions.accept] {
            std::vector<std::size_t> picked;
            for (std::size_t i = 0; i < checked->size(); ++i) {
                if ((*checked)[i]) {
                    picked.push_back(i);
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
    return AppsDialog{std::move(dialog), table};
}

} // namespace wl::app
