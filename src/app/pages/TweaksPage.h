#pragma once
// P12 Ayarlar / Tweaks (docs/pages/12-tweaks.md, screen 10): tabs + a form. Each tab lists its
// sections (caps title + rule) and 32px rows: label (240px column) and a toggle (+ hint), a
// dropdown or a radio group. The controls show what the queue says (ImageSettingsController).
#include "app/Localization.h"
#include "app/controllers/ImageSettingsController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <vector>

namespace wl::app {

class TweaksPage : public ui::Widget {
public:
    TweaksPage(AppState& state, ImageSettingsController& controller, const Localization& strings, Language language,
               std::function<void()> goImages);
    ~TweaksPage() override;

    void layout() override;

private:
    struct Binding { // one form row ↔ one catalog setting (exactly one control is set)
        const ImageSetting* setting;
        ui::Toggle* toggle;
        ui::Dropdown* dropdown;
        ui::RadioGroup* radio;
    };
    void refresh();
    void showTab(const std::string& tab);
    void addSetting(const ImageSetting& setting);
    void sync(); // control positions from the queue

    AppState& m_state;
    ImageSettingsController& m_controller;
    Language m_language;
    std::size_t m_subscription = 0;
    std::vector<Binding> m_bindings;
    ui::TabBar* m_tabs = nullptr;
    ui::FormView* m_form = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
