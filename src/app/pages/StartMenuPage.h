#pragma once
// D-069 Başlat menüsü: two tabs.
//   Sabitlenenler — the mode (Windows default / empty / my list) and "users may change it later";
//     with "my list": the image's apps on the left (search, double click / Enter adds) and on the
//     right a Start-like preview that is the pin list itself (select; Delete removes; Ctrl+Left /
//     Right or the buttons move; order = Start's order).
//   Başlat ayarları — the "start" tab of the settings catalog (TweaksPage, one tab).
#include "app/Localization.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/StartPinsController.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TabBar.h"

#include <functional>

namespace wl::ui {
class Button;
class CheckField;
class RadioGroup;
class SearchBox;
} // namespace wl::ui

namespace wl::app {

class TweaksPage;

class StartMenuPage : public ui::Widget {
public:
    StartMenuPage(AppState& state, StartPinsController& pins, ImageSettingsController& settings, const Localization& strings,
                  Language language, std::function<void()> goImages);
    ~StartMenuPage() override;

    void showTab(int tab);
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    class AppList;
    class PinGrid;
    void refresh();
    void sync(); // controls from the queue

    AppState& m_state;
    StartPinsController& m_pins;
    const Localization& m_strings;
    std::size_t m_subscription = 0;
    ui::TabBar* m_tabs = nullptr;
    ui::RadioGroup* m_mode = nullptr;
    ui::CheckField* m_once = nullptr;
    ui::SearchBox* m_search = nullptr;
    AppList* m_apps = nullptr;
    PinGrid* m_grid = nullptr;
    ui::Button* m_add = nullptr;
    ui::Button* m_left = nullptr;
    ui::Button* m_right = nullptr;
    ui::Button* m_remove = nullptr;
    ui::Button* m_clear = nullptr;
    TweaksPage* m_settings = nullptr;
    ui::EmptyState* m_empty = nullptr;
    int m_tab = 0;
};

} // namespace wl::app
