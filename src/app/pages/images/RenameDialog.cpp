#include "app/pages/images/RenameDialog.h"

#include "ui/widgets/FormView.h"
#include "ui/widgets/SearchBox.h"

namespace wl::app {

namespace {

constexpr float kWidth = 520.0f;
constexpr float kLabelWidth = 96.0f;
constexpr float kFieldWidth = 376.0f;

struct Draft {
    std::wstring name;
    std::wstring description;
    std::wstring firstName;
    std::wstring firstDescription;

    [[nodiscard]] bool ready() const {
        return name.find_first_not_of(L' ') != std::wstring::npos && name.size() <= 255 &&
               (name != firstName || description != firstDescription);
    }
};

} // namespace

RenameDialog makeRenameDialog(const Localization& strings, std::wstring name, std::wstring description, std::wstring note,
                              RenameDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    auto dialog = std::make_unique<ui::Dialog>(s(Str::DialogsRenameTitle), std::move(note), ui::icons::Icon::Edit,
                                               ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto draft = std::make_shared<Draft>(Draft{name, description, name, description});
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow * 2, kLabelWidth);

    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto refresh = [draft, primary] {
        if (*primary) {
            (*primary)->setEnabled(draft->ready());
        }
    };
    auto accept = [draft, close = actions.close, done = actions.accept] {
        if (!draft->ready()) {
            return;
        }
        const Draft result = *draft; // closing destroys what holds the draft
        close();
        done(result.name, result.description);
    };
    auto text = [&](Str label, std::wstring Draft::*field) -> ui::SearchBox& {
        auto& box = form.addRow<ui::SearchBox>(s(label), std::wstring(), kFieldWidth, std::wstring());
        box.setPlain(true);
        box.setText((*draft).*field);
        box.setAccessible(ui::AccessRole::Edit, s(label));
        box.onChange = [draft, field, refresh](const std::wstring& value) {
            (*draft).*field = value;
            refresh();
        };
        box.onSubmit = accept;
        return box;
    };
    ui::Widget* focus = &text(Str::CommonName, &Draft::name);
    text(Str::DialogsRenameDescription, &Draft::description);

    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    *primary = &raw->addButton(ui::ButtonKind::Primary, s(Str::CommonSave), accept, /*primary=*/true);
    raw->onCancel = actions.close;
    refresh();
    return RenameDialog{std::move(dialog), focus};
}

} // namespace wl::app
