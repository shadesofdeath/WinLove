#pragma once
// D-069 Başlat menüsü: three tabs.
//   Sabitlenenler — the mode (Windows default / empty / my list) and "users may change it later";
//     with "my list": the image's apps on the left (search, double click / Enter adds) and on the
//     right a Start-like preview that is the pin list itself (select; Delete removes; Ctrl+Left /
//     Right or the buttons move; order = Start's order).
//   Görev çubuğu (D-083) — the same for the taskbar: Windows' pins, none, or my list (left to right),
//     "users may unpin"; Windows 11 images only.
//   Başlat ayarları — the "start" tab of the settings catalog (TweaksPage, one tab).
// The list's actions (Ekle | Öne al · Geriye al · Kaldır | Temizle) are the page's ribbon (2026-11-10:
// they were in the body and the page had no band): the Shell makes the buttons, attachRibbon()
// gives them to the page, which runs them and keeps them enabled as before (my list, a pin tab).
#include "app/Localization.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/StartPinsController.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TabBar.h"

#include <cstdint>
#include <functional>
#include <vector>

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
    StartMenuPage(AppState& state, StartPinsController& pins, StartPinsController& taskbar, ImageSettingsController& settings,
                  const Localization& strings, Language language, std::function<void()> goImages);
    static constexpr int kTabPins = 0;
    static constexpr int kTabTaskbar = 1;
    static constexpr int kTabSettings = 2;
    ~StartMenuPage() override;

    void showTab(int tab);
    struct Ribbon {
        ui::Button* add = nullptr;
        ui::Button* left = nullptr;
        ui::Button* right = nullptr;
        ui::Button* remove = nullptr;
        ui::Button* clear = nullptr;
    };
    void attachRibbon(Ribbon ribbon);
    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    class AppList;
    class PinGrid;
    void refresh();
    void sync(); // controls from the queue

    // The tab's plan as the queue has it (audit PG-11). The controller reads it back from the
    // queue's operations on every call, and one frame asked 4-6 times (the list, the preview, the
    // note, the layout), a pointer move over the preview once more. Taken again after every state
    // change (AppState::revision), for the other controller, and after touched().
    struct PinsSnapshot {
        StartPinsController::Mode mode = StartPinsController::Mode::Windows;
        std::vector<core::StartApp> pins;
        std::uint64_t at = ~std::uint64_t{0};
        const StartPinsController* of = nullptr;
    };
    [[nodiscard]] const PinsSnapshot& snapshot() const;
    // After anything this page tells the controller, and when the image's apps arrive: neither
    // always reaches the state (a frozen queue keeps "my list" in the controller alone).
    void touched() noexcept { m_snapshot.at = ~std::uint64_t{0}; }
    mutable PinsSnapshot m_snapshot;

    [[nodiscard]] bool taskbar() const noexcept { return m_tab == kTabTaskbar; }
    [[nodiscard]] bool windows10() const; // the selected edition is older than Windows 11

    AppState& m_state;
    StartPinsController& m_start;
    StartPinsController& m_taskbar;
    StartPinsController* m_pins; // the tab's: Start or the taskbar
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
