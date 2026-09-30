#include "app/pages/postsetup/StepDialog.h"

#include "app/controllers/PostSetupController.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/SearchBox.h"

namespace wl::app {

using Step = core::PostSetupStep;

namespace {

constexpr float kWidth = 520.0f;
constexpr float kLabelWidth = 128.0f;
constexpr float kFieldWidth = 344.0f;

bool valid(const Step& step) {
    core::PostSetupPlan plan;
    plan.steps.push_back(step);
    return core::validatePostSetup(plan).empty();
}

// A step without a name is listed by what it does.
std::wstring defaultName(const Step& step) {
    if (step.type == Step::Type::Copy) {
        return std::filesystem::path(step.source).filename().wstring();
    }
    return step.source.size() > 48 ? step.source.substr(0, 48) : step.source;
}

} // namespace

StepDialog makeStepDialog(const Localization& strings, Step initial, bool editing, StepDialogActions actions) {
    auto s = [&](Str key) { return strings.get(key); };
    const Str title = editing                                ? Str::PostsetupEditTitle
                      : initial.type == Step::Type::Winget   ? Str::PostsetupAddApp
                      : initial.type == Step::Type::Copy     ? Str::PostsetupAddFile
                                                             : Str::PostsetupAddCommand;
    auto dialog = std::make_unique<ui::Dialog>(s(title), std::wstring(), std::nullopt, ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto draft = std::make_shared<Step>(std::move(initial));
    if (draft->type == Step::Type::Copy && draft->destination.empty()) {
        draft->destination = L"%PUBLIC%\\Desktop";
    }
    const int rows = 3;
    auto& form = raw->setContent<ui::FormView>(ui::FormView::kRow * rows, kLabelWidth);

    // Filled in below; the buttons and boxes refer to each other.
    auto primary = std::make_shared<ui::Button*>(nullptr);
    auto refresh = [draft, primary] {
        if (*primary) {
            (*primary)->setEnabled(valid(*draft));
        }
    };
    auto accept = [draft, close = actions.close, done = actions.accept] {
        if (!valid(*draft)) {
            return;
        }
        Step step = *draft;
        if (step.name.empty()) {
            step.name = defaultName(step);
        }
        close();
        done(std::move(step));
    };
    auto text = [&](Str label, std::wstring Step::*field, std::optional<Str> tooltip) -> ui::SearchBox& {
        auto& box = form.addRow<ui::SearchBox>(s(label), std::wstring(), kFieldWidth, std::wstring());
        box.setPlain(true);
        box.setText((*draft).*field);
        box.setAccessible(ui::AccessRole::Edit, s(label));
        if (tooltip) {
            box.setTooltip(s(*tooltip));
        }
        box.onChange = [draft, field, refresh](const std::wstring& value) {
            (*draft).*field = value;
            refresh();
        };
        box.onSubmit = accept;
        return box;
    };

    ui::Widget* focus = nullptr;
    ui::SearchBox* source = nullptr;
    switch (draft->type) {
    case Step::Type::Winget: {
        std::vector<std::wstring> items{s(Str::PostsetupPopularNone)};
        for (const auto& app : PostSetupController::popularApps()) {
            items.push_back(app.name);
        }
        auto& popular = form.addRow<ui::Dropdown>(s(Str::PostsetupPopular), std::wstring(), kFieldWidth, std::wstring(),
                                                  std::move(items), 0);
        auto& name = text(Str::PostsetupName, &Step::name, std::nullopt);
        auto& id = text(Str::PostsetupWingetId, &Step::source, std::nullopt);
        popular.onChange = [draft, refresh, &name, &id](int index) {
            const auto& apps = PostSetupController::popularApps();
            if (index >= 1 && index <= static_cast<int>(apps.size())) {
                const auto& app = apps[static_cast<std::size_t>(index - 1)];
                draft->name = app.name;
                draft->source = app.id;
                name.setText(app.name);
                id.setText(app.id);
                refresh();
            }
        };
        focus = &id;
        break;
    }
    case Step::Type::Copy: {
        text(Str::PostsetupName, &Step::name, std::nullopt);
        source = &text(Str::PostsetupSourcePath, &Step::source, Str::PostsetupPickTitle);
        text(Str::PostsetupDestination, &Step::destination, Str::PostsetupDestinationHint);
        focus = source;
        break;
    }
    case Step::Type::Command: {
        text(Str::PostsetupName, &Step::name, std::nullopt);
        focus = &text(Str::PostsetupCommand, &Step::source, Str::PostsetupCommandHint);
        auto& wait = form.addRow<ui::CheckField>(s(Str::PostsetupWait), std::wstring(), 0.0f, s(Str::PostsetupWaitForIt),
                                                 draft->wait);
        wait.onChange = [draft](bool on) { draft->wait = on; };
        break;
    }
    }

    if (source) {
        auto pick = [draft, refresh, source](const std::function<std::optional<std::filesystem::path>()>& chooser) {
            if (!chooser) {
                return;
            }
            if (const auto path = chooser()) {
                draft->source = path->wstring();
                source->setText(draft->source);
                refresh();
            }
        };
        raw->addButton(ui::ButtonKind::Secondary, s(Str::PostsetupPickFile), [pick, chooser = actions.pickFile] { pick(chooser); });
        raw->addButton(ui::ButtonKind::Secondary, s(Str::PostsetupPickFolder),
                       [pick, chooser = actions.pickFolder] { pick(chooser); });
    }
    raw->addButton(ui::ButtonKind::Secondary, s(Str::CommonCancel), actions.close);
    *primary = &raw->addButton(ui::ButtonKind::Primary, s(editing ? Str::CommonSave : Str::CommonAdd), accept, /*primary=*/true);
    raw->onCancel = actions.close;
    refresh();
    return StepDialog{std::move(dialog), focus};
}

} // namespace wl::app
