#include "app/pages/images/EditionDialog.h"

#include "ui/widgets/Dropdown.h"
#include "ui/widgets/FormView.h"

#include <algorithm>

namespace wl::app {

namespace {
constexpr float kWidth = 520.0f;
constexpr float kLabelWidth = 96.0f;
constexpr float kFieldWidth = 376.0f;
} // namespace

EditionDialog makeEditionDialog(const Localization& strings, std::wstring body, std::vector<EditionChoice> choices,
                                const std::wstring& queued, EditionDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::DialogsUpgradeTitle), std::move(body), ui::icons::Icon::LayersEditions,
                                               ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();

    // Preselected: what is queued; else Pro, the change most people are after; else the first.
    auto at = std::ranges::find(choices, queued, &EditionChoice::id);
    if (at == choices.end()) {
        at = std::ranges::find(choices, std::wstring(L"Professional"), &EditionChoice::id);
    }
    auto picked = std::make_shared<int>(at == choices.end() ? 0 : static_cast<int>(at - choices.begin()));
    auto ids = std::make_shared<std::vector<std::wstring>>();
    std::vector<std::wstring> names;
    for (auto& choice : choices) {
        ids->push_back(std::move(choice.id));
        names.push_back(std::move(choice.name));
    }

    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow, kLabelWidth);
    auto& target = form.addRow<ui::Dropdown>(s(Str::DialogsUpgradeTarget), std::wstring(), kFieldWidth, std::wstring(),
                                             std::move(names), *picked);
    target.setAccessible(ui::AccessRole::Group, s(Str::DialogsUpgradeTarget));
    target.onChange = [picked](int index) { *picked = index; };

    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    if (!queued.empty()) {
        raw->addButton(ui::ButtonKind::Secondary, s(Str::DialogsUpgradeRemove), [close = actions.close, remove = actions.remove] {
            close();
            remove();
        });
    }
    ui::Button& add = raw->addButton(ui::ButtonKind::Primary, s(Str::DialogsUpgradeQueue),
                   [picked, ids, close = actions.close, accept = actions.accept] {
                       if (*picked < 0 || *picked >= static_cast<int>(ids->size())) {
                           return;
                       }
                       std::wstring id = (*ids)[static_cast<std::size_t>(*picked)];
                       close();
                       accept(std::move(id));
                   },
                   /*primary=*/true);
    raw->onCancel = actions.close;
    return EditionDialog{std::move(dialog), &add};
}

} // namespace wl::app
