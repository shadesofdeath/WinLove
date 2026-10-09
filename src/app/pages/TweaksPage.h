#pragma once
// P12 Ayarlar / Tweaks (docs/pages/12-tweaks.md, screen 10): tabs + a form. Each tab lists its
// sections (caps title + rule) and 32px rows: label (240px column) and a toggle (+ hint), a
// dropdown or a radio group. The controls show what the queue says (ImageSettingsController).
// D-091: a search and "Yalnız değişenler" over every tab (the form then holds all tabs, titled
// "Tab › Section"), changed rows marked, each tab's count of changes on the tab, and hints that
// only say something when there is something to say (no "Açık · Windows varsayılanı" on every row).
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
#include <string>
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
    // onlyTab: one tab of the catalog, without the tab bar (D-069: the Başlat menüsü page shows "start").
    TweaksPage(AppState& state, ImageSettingsController& controller, const Localization& strings, Language language,
               std::function<void()> goImages, PickImage pickImage = {}, std::string onlyTab = {});
    ~TweaksPage() override;

    // Command palette: shows the setting's tab and puts the focus on its control (the form
    // scrolls the focused control into view).
    void reveal(const std::string& settingId);

    void layout() override;
    void paint(ui::Canvas& canvas) override;

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
    [[nodiscard]] static const ui::Widget* controlOf(const Binding& b);
    void valueTyped(std::size_t binding, const std::wstring& value);
    void refresh();
    void showTab(const std::string& tab);
    void rebuild();     // the form for the tab, or for every tab while filtering
    void applyFilter(); // which rows the search and "only changed" leave
    void updateCounts(); // the tabs' badges and the summary
    [[nodiscard]] bool filtering() const;
    [[nodiscard]] bool changed(const ImageSetting& setting) const;
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
    std::string m_onlyTab;
    ui::TabBar* m_tabs = nullptr;
    ui::FormView* m_form = nullptr;
    ui::SearchBox* m_search = nullptr;
    ui::Toggle* m_onlyChanged = nullptr;
    std::wstring m_query;    // folded (text::fold)
    std::string m_tab;       // the tab shown when not filtering
    bool m_allTabs = false;  // the form holds every tab
    std::wstring m_summary;  // "6 ayar değişecek" right of the filters
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
