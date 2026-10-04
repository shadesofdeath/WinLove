#pragma once
// D-065 Simgeler: the Windows icons of the image an icon pack replaces, as cards in two groups
// (MASAÜSTÜ, GEZGİN): what the image shows now → what it gets, the name, the icon file. Click a
// card (or Enter) to pick an .ico, Delete / right-click to go back to Windows' own. Header actions
// (Shell): "İkon paketi yükle…", "Kısayol okunu kaldır", "Tümünü sıfırla".
#include "app/Localization.h"
#include "app/controllers/IconController.h"
#include "ui/widgets/EmptyState.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace wl::app {

class IconsPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> goImages;
        std::function<std::optional<std::filesystem::path>()> pickIcon; // a file dialog for one .ico
        std::function<void(const std::wstring& file)> refused;          // not an icon
    };
    IconsPage(AppState& state, IconController& controller, const Localization& strings, Language language, Intents intents);
    ~IconsPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;

private:
    class Grid;
    void refresh();

    AppState& m_state;
    IconController& m_controller;
    const Localization& m_strings;
    Intents m_intents;
    std::size_t m_subscription = 0;
    Grid* m_grid = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
