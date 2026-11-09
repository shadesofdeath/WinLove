#include "app/pages/components/ModeDialog.h"

#include "ui/widgets/TableView.h"

#include <algorithm>
#include <array>

namespace wl::app {

namespace {

using ui::tokens::Color;
using ui::tokens::TypeStyle;
using Level = ComponentController::Level;

constexpr float kWidth = 720.0f;
constexpr float kRow = 44.0f; // the name over its two-line description
enum Column { kLevel, kWhat };

struct Choice {
    Level level;
    Str name;
    Str description;
};
constexpr std::array<Choice, 3> kChoices = {{
    {Level::DefaultPlus, Str::ComponentsLevelDefault, Str::ComponentsLevelDefaultDesc},
    {Level::Deep, Str::ComponentsLevelDeep, Str::ComponentsLevelDeepDesc},
    {Level::Maximum, Str::ComponentsLevelMax, Str::ComponentsLevelMaxDesc},
}};

} // namespace

ModeDialog makeModeDialog(const Localization& strings, Level level, ModeDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::ComponentsModeTitle), s(Str::ComponentsModeBody), ui::icons::Icon::ShieldCheck,
                                               Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto chosen = std::make_shared<int>(static_cast<int>(level));
    auto* table = &raw->setContent<ui::TableView>(kRow * static_cast<float>(kChoices.size()) + 2,
                                                  std::vector<ui::TableColumn>{{L"", 150}, {L"", 0}});
    table->setHeaderHeight(0);
    table->setRowHeight(kRow);
    table->setRowCount(static_cast<int>(kChoices.size()));
    table->setSelected(static_cast<int>(level), /*reveal=*/false);
    table->setAccessible(ui::AccessRole::Group, s(Str::ComponentsModeTitle));
    auto pick = [table, chosen](int row) {
        if (row >= 0 && row < static_cast<int>(kChoices.size())) {
            *chosen = row;
            table->refresh();
        }
    };
    table->onCellClick = [pick](int row, int, ui::PointF) { pick(row); };
    table->onSelect = pick;
    table->paintCell = [&strings, chosen](ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState) {
        if (row < 0 || row >= static_cast<int>(kChoices.size())) {
            return;
        }
        const Choice& c = kChoices[static_cast<std::size_t>(row)];
        if (column == kLevel) {
            const ui::PointF center{rect.x + 7, rect.y + rect.height / 2};
            canvas.strokeRoundRect({center.x - 7, center.y - 7, 14, 14}, 7, Color::TextTertiary);
            if (*chosen == row) {
                canvas.fillEllipse(center, 4, Color::AccentBase);
            }
            canvas.drawText(strings.get(c.name), {rect.x + 22, rect.y, rect.width - 22, rect.height}, TypeStyle::BodySemi,
                            Color::TextPrimary);
        } else {
            canvas.drawTextWrapped(strings.get(c.description), {rect.x, rect.y + 6, rect.width, rect.height - 8}, TypeStyle::Caption,
                                   Color::TextSecondary);
        }
    };
    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    raw->addButton(
        ui::ButtonKind::Primary, s(Str::CommonOk),
        [chosen, close = actions.close, save = actions.save] {
            const Level picked = kChoices[static_cast<std::size_t>(std::clamp(*chosen, 0, 2))].level;
            close();
            save(picked);
        },
        /*primary=*/true);
    raw->onCancel = actions.close;
    return ModeDialog{std::move(dialog), table};
}

} // namespace wl::app
