#pragma once
// P09 Sürücüler (docs/pages/09-drivers.md, screen 07): INF packages from scanned folders as a
// class → INF tri-state tree (Ad · Sağlayıcı · Sürüm · Boyut). Checked INFs are queued as AddDriver.
// Toolbar: search ("/"), Sınıf, Mimari (defaults to the mounted edition's architecture).
// Second tab "İmajdaki sürücüler" (D-052): the image's third-party drivers — Sürücü (checkbox =
// remove) · Sınıf · Sağlayıcı · Sürüm · Tarih; a boot-critical one says so.
#include "app/Localization.h"
#include "app/controllers/ImageDriverController.h"
#include "app/state/AppState.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <set>

namespace wl::app {

class DriversPage : public ui::Widget {
public:
    struct Intents {
        std::function<void()> scanFolder;
        std::function<void()> goImages;
    };
    DriversPage(AppState& state, ImageDriverController& images, const Localization& strings, Language language,
                Intents intents);
    ~DriversPage() override;

    void focusSearch();
    void showImageTab(); // render demo: the second tab without reading DISM
    // Friendly class name ("Ağ (Net)") for the common setup classes, else the class itself.
    [[nodiscard]] static std::wstring className(const std::wstring& cls, const Localization& strings);

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    struct Group {
        std::wstring cls;
        std::vector<int> infs; // indexes into driverScan().infs (visible after filters)
        int total = 0;
        std::uint64_t size = 0;
    };
    struct Row {
        int group = 0;
        int inf = -1; // index into driverScan().infs; -1 = group row
    };
    void refresh();
    void rebuild();
    [[nodiscard]] bool queued(int inf) const;
    void toggle(int inf);
    void toggleGroup(const Group& group);
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void paintImageCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    [[nodiscard]] bool imageTab() const { return m_tabs->selected() == 1; }

    AppState& m_state;
    ImageDriverController& m_images;
    const Localization& m_strings;
    Language m_language;
    Intents m_intents;
    std::size_t m_subscription = 0;
    std::vector<Group> m_groups;
    std::vector<Row> m_rows;
    std::set<std::wstring> m_collapsed;
    std::wstring m_needle;
    int m_classFilter = 0;
    int m_archFilter = 0; // 0 all, 1 x64, 2 arm64, 3 x86
    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_class = nullptr;
    ui::Dropdown* m_arch = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
    ui::TabBar* m_tabs = nullptr;
    ui::TableView* m_imageTable = nullptr;
};

} // namespace wl::app
