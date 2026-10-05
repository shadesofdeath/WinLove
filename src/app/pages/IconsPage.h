#pragma once
// D-068 Simgeler, two tabs: "Sistem simgeleri" (IconFilesView: every icon inside the image's icon
// files, patched in place) and "Masaüstü ve Gezgin" (D-065, below).
// D-065: the Windows icons of the image an icon pack replaces, as cards in two groups
// (MASAÜSTÜ, GEZGİN): what the image shows now → what it gets, the name, the icon file. Click a
// card (or Enter) to pick an .ico, Delete / right-click to go back to Windows' own. Header actions
// (Shell): "İkon paketi yükle…", "Kısayol okunu kaldır", "Tümünü sıfırla".
#include "app/Localization.h"
#include "app/controllers/IconController.h"
#include "app/pages/icons/IconFilesView.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/TabBar.h"

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
        IconFilesView::Intents files;                                     // the system icons tab
    };
    IconsPage(AppState& state, IconController& controller, IconPatchController& patches, const Localization& strings,
              Language language, Intents intents);
    ~IconsPage() override;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    void showTab(int tab);
    [[nodiscard]] IconFilesView* filesView() const { return m_files; } // demo

private:
    class Grid;
    void refresh();

    AppState& m_state;
    IconController& m_controller;
    const Localization& m_strings;
    Intents m_intents;
    std::size_t m_subscription = 0;
    Grid* m_grid = nullptr;
    IconFilesView* m_files = nullptr;
    ui::TabBar* m_tabs = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
