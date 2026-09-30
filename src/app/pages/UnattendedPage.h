#pragma once
// P13 Katılımsız Kurulum (docs/pages/13-unattended.md, screen 11): step indicator, the form on
// the left (language, account, disk, OOBE, product key, requirements) and the live
// autounattend.xml preview on the right — every edit rebuilds the text, changed lines in accent.
// The steps are anchors into one scrolling form, not wizard pages.
#include "app/Localization.h"
#include "app/controllers/UnattendController.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wl::app {

class StepBar;
class XmlPreview;

class UnattendedPage : public ui::Widget {
public:
    UnattendedPage(AppState& state, UnattendController& controller, const Localization& strings);
    ~UnattendedPage() override;

    void showStep(int index); // scrolls the form to a section (step click, --demo-unattended)
    void layout() override;

private:
    using Options = core::UnattendOptions;
    // A dropdown over UnattendController::Choice values; item 0 is "ask during setup" (empty value).
    struct Picker {
        ui::Dropdown* box = nullptr;
        std::wstring firstLabel;
        std::vector<UnattendController::Choice> choices;
        std::function<std::wstring(const Options&)> get;
        std::vector<std::wstring> values; // what each item writes; rebuilt by sync()
    };
    struct TextField {
        ui::SearchBox* box = nullptr;
        std::function<std::wstring(const Options&)> get;
    };
    struct Switch {
        ui::Toggle* toggle = nullptr;
        std::function<bool(const Options&)> get;
        std::wstring offHint; // shown while the switch is off ("Atla (LabConfig)")
    };

    void buildForm(); // choices come from the open source: rebuilt when it changes
    void addPicker(Str label, std::wstring firstLabel, std::vector<UnattendController::Choice> choices,
                   std::function<std::wstring(const Options&)> get, std::function<void(Options&, const std::wstring&)> set);
    ui::SearchBox& addText(Str label, std::optional<Str> hint, float width,
                           std::function<std::wstring(const Options&)> get,
                           std::function<void(Options&, const std::wstring&)> set);
    void addSwitch(Str label, std::wstring offHint, std::function<bool(const Options&)> get,
                   std::function<void(Options&, bool)> set);
    void sync(); // controls, hints and the preview from the options

    AppState& m_state;
    UnattendController& m_controller;
    const Localization& m_strings;
    std::size_t m_subscription = 0;
    StepBar* m_steps = nullptr;
    ui::FormView* m_form = nullptr;
    XmlPreview* m_preview = nullptr;
    ui::CheckField* m_include = nullptr;
    std::vector<Picker> m_pickers;
    std::vector<TextField> m_texts;
    std::vector<Switch> m_switches;
    ui::SearchBox* m_account = nullptr;
    ui::SearchBox* m_computer = nullptr;
    ui::SearchBox* m_key = nullptr;
    ui::CheckField* m_autoLogon = nullptr;
    ui::Dropdown* m_disk = nullptr;
};

} // namespace wl::app
