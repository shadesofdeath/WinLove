#pragma once
// P12 Ayarlar / Tweaks (docs/pages/12-tweaks.md, screen 10): tabs + a form. Each tab lists its
// sections (caps title + rule) and 32px rows: label (240px column) and a toggle (+ hint), a
// dropdown or a radio group. The controls show what the queue says (ImageSettingsController).
#include "app/Localization.h"
#include "app/controllers/ImageSettingsController.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TabBar.h"

#include <functional>

namespace wl::app {

class SettingsForm;

class TweaksPage : public ui::Widget {
public:
    TweaksPage(AppState& state, ImageSettingsController& controller, const Localization& strings, Language language,
               std::function<void()> goImages);
    ~TweaksPage() override;

    void layout() override;

private:
    void refresh();

    AppState& m_state;
    ImageSettingsController& m_controller;
    const Localization& m_strings;
    std::size_t m_subscription = 0;
    ui::TabBar* m_tabs = nullptr;
    SettingsForm* m_form = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
