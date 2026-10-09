#include "app/pages/postsetup/StepDialog.h"

#include "app/controllers/PostSetupController.h"
#include "core/postsetup/Wifi.h"
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
                      : initial.type == Step::Type::Wifi     ? Str::PostsetupAddWifi
                      : initial.type == Step::Type::PowerPlan ? Str::PostsetupAddPower
                                                             : Str::PostsetupAddCommand;
    // Wi-Fi: where the key ends up is said up front.
    const std::wstring message = initial.type == Step::Type::Wifi ? s(Str::PostsetupWifiKeyHint) : std::wstring();
    auto dialog = std::make_unique<ui::Dialog>(s(title), message, std::nullopt, ui::tokens::Color::TextSecondary, kWidth);
    ui::Dialog* raw = dialog.get();
    auto draft = std::make_shared<Step>(std::move(initial));
    if (draft->type == Step::Type::Copy && draft->destination.empty()) {
        draft->destination = L"%PUBLIC%\\Desktop";
    }
    const int rows = draft->type == Step::Type::Wifi ? 5 : 3;
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
    case Step::Type::Wifi: {
        // D-056: a network typed in, or one of this PC's profiles (its key is readable elevated).
        auto net = std::make_shared<core::WifiNetwork>(draft->source.empty() ? core::WifiNetwork{}
                                                                             : core::wifiNetworkFromXml(draft->source));
        auto hosts = std::make_shared<std::vector<core::HostWifiProfile>>();
        if (auto list = core::hostWifiProfiles()) {
            *hosts = std::move(*list);
        }
        std::vector<std::wstring> items{s(Str::PostsetupWifiManual)};
        for (const auto& h : *hosts) {
            items.push_back(h.portable ? h.name : h.name + L" \u00B7 " + s(Str::PostsetupWifiProtected));
        }
        auto& from = form.addRow<ui::Dropdown>(s(Str::PostsetupWifiFromPc), std::wstring(), kFieldWidth, std::wstring(),
                                               std::move(items), 0);
        from.setEnabled(!hosts->empty());
        auto& ssid = form.addRow<ui::SearchBox>(s(Str::PostsetupWifiSsid), std::wstring(), kFieldWidth, std::wstring());
        ssid.setPlain(true);
        ssid.setText(net->ssid);
        ssid.setAccessible(ui::AccessRole::Edit, s(Str::PostsetupWifiSsid));
        auto& key = form.addRow<ui::SearchBox>(s(Str::PostsetupWifiPassword), std::wstring(), kFieldWidth, std::wstring());
        key.setPlain(true);
        key.setPassword(true);
        key.setText(net->password);
        key.setAccessible(ui::AccessRole::Edit, s(Str::PostsetupWifiPassword));
        auto& security = form.addRow<ui::Dropdown>(
            s(Str::PostsetupWifiSecurity), std::wstring(), kFieldWidth, std::wstring(),
            std::vector<std::wstring>{L"WPA2-Personal", L"WPA3-Personal", s(Str::PostsetupWifiOpen)}, static_cast<int>(net->security));
        auto& hidden = form.addRow<ui::CheckField>(s(Str::PostsetupWifiHidden), std::wstring(), 0.0f,
                                                   s(Str::PostsetupWifiHiddenHint), net->hidden);
        // Typed fields: the profile is ours, rebuilt from them (empty while they are not valid).
        auto rebuild = [draft, net, refresh, &from] {
            from.setSelected(0);
            draft->name = net->ssid;
            draft->source = core::validateWifi(*net) == core::WifiProblem::None ? core::wifiProfileXml(*net) : std::wstring();
            refresh();
        };
        ssid.onChange = [net, rebuild](const std::wstring& v) {
            net->ssid = v;
            rebuild();
        };
        key.onChange = [net, rebuild](const std::wstring& v) {
            net->password = v;
            rebuild();
        };
        security.onChange = [net, rebuild](int index) {
            net->security = static_cast<core::WifiSecurity>(std::clamp(index, 0, 2));
            rebuild();
        };
        hidden.onChange = [net, rebuild](bool on) {
            net->hidden = on;
            rebuild();
        };
        ssid.onSubmit = accept;
        key.onSubmit = accept;
        // This PC's profile: taken as it is (other settings it has stay); a protected key cannot go.
        from.onChange = [draft, net, hosts, refresh, &ssid, &key, &security, &hidden](int index) {
            if (index < 1 || index > static_cast<int>(hosts->size())) {
                return;
            }
            const auto& h = (*hosts)[static_cast<std::size_t>(index - 1)];
            *net = core::wifiNetworkFromXml(h.xml);
            if (!h.portable) {
                net->password.clear();
            }
            ssid.setText(net->ssid);
            key.setText(net->password);
            security.setSelected(static_cast<int>(net->security));
            hidden.setChecked(net->hidden);
            draft->name = h.name;
            draft->source = h.portable ? h.xml : std::wstring();
            refresh();
        };
        focus = &ssid;
        break;
    }
    case Step::Type::PowerPlan: {
        text(Str::PostsetupName, &Step::name, std::nullopt);
        source = &text(Str::PostsetupSourcePath, &Step::source, Str::PostsetupPowerPick);
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
