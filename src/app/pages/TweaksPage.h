#pragma once
// P12 Ayarlar / Tweaks (docs/pages/12-tweaks.md, screen 10): tabs + a form. Each tab lists its
// sections (caps title + rule) and 32px rows: label (240px column) and a toggle (+ hint), a
// dropdown or a radio group. The controls show what the queue says (ImageSettingsController).
#include "app/Localization.h"
#include "app/controllers/ImageSettingsController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/Toggle.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace wl::app {

// A path box with a browse button: the control of a "file" setting.
class PictureField : public ui::Widget {
public:
    explicit PictureField(std::wstring browseTooltip);
    std::function<void(const std::wstring&)> onChange; // typed
    std::function<void()> onBrowse;

    void setText(std::wstring text) { m_box->setText(std::move(text)); }
    [[nodiscard]] const std::wstring& text() const { return m_box->text(); }
    [[nodiscard]] ui::SearchBox& box() { return *m_box; }

    [[nodiscard]] ui::SizeF measure(ui::SizeF available) override;
    void layout() override;

private:
    ui::SearchBox* m_box = nullptr;
    ui::Button* m_browse = nullptr;
};

class TweaksPage : public ui::Widget {
public:
    using PickImage = std::function<std::optional<std::filesystem::path>()>;
    TweaksPage(AppState& state, ImageSettingsController& controller, const Localization& strings, Language language,
               std::function<void()> goImages, PickImage pickImage = {});
    ~TweaksPage() override;

    // Command palette: shows the setting's tab and puts the focus on its control (the form
    // scrolls the focused control into view).
    void reveal(const std::string& settingId);

    void layout() override;

private:
    struct Binding { // one form row ↔ one catalog setting (exactly one control is set)
        const ImageSetting* setting = nullptr;
        ui::Toggle* toggle = nullptr;
        ui::Dropdown* dropdown = nullptr;
        ui::RadioGroup* radio = nullptr;
        ui::SearchBox* text = nullptr;
        PictureField* file = nullptr;
        std::wstring shown;   // text / file: the queue's value the box was last in step with
        bool problem = false; // file: what is typed is not a file the image can take
    };
    void valueTyped(std::size_t binding, const std::wstring& value);
    void refresh();
    void showTab(const std::string& tab);
    void addSetting(const ImageSetting& setting);
    void sync(); // control positions from the queue

    AppState& m_state;
    ImageSettingsController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<Binding> m_bindings;
    std::size_t m_typing = static_cast<std::size_t>(-1); // the binding whose box is being typed into
    PickImage m_pickImage;
    ui::TabBar* m_tabs = nullptr;
    ui::FormView* m_form = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
