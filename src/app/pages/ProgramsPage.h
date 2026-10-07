#pragma once
// D-078 Programlar: winget programs the installed Windows gets at the first sign-in. Toolbar:
// search over the whole repository ("/"), Kategori, "Yalnızca seçili", and on the right how many
// are picked. Under it the bundles (BundleStrip): a click picks a bundle's programs. Before a
// search the catalog's categories are listed (programs.json) — each with its featured programs and
// a "N program daha" row that opens the category: then its featured programs, then every package
// winget tags as it. Kategori's last entry is the whole repository. The picks from searches come at
// the end; a search lists the repository's best matches. Table: Program (box, icon, name) · Kimlik
// · Sürüm, 24px rows. The selected program goes to the Shell's inspector column (ProgramInspector).
#include "app/Localization.h"
#include "app/controllers/ProgramsController.h"
#include "app/pages/programs/BundleStrip.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <optional>

namespace wl::app {

class ProgramsPage : public ui::Widget {
public:
    ProgramsPage(AppState& state, ProgramsController& controller, const Localization& strings, Language language,
                 std::function<void()> goToImages);
    ~ProgramsPage() override;

    std::function<void()> onSelectionChanged;
    [[nodiscard]] std::optional<core::WingetPackage> selectedProgram() const;
    void focusSearch();

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    struct Row {
        bool group = false;
        std::wstring title; // group rows
        int count = 0;      // group rows
        core::WingetPackage package;
        int more = -1; // a "N program daha" row: the category it opens
    };
    void refresh();
    void rebuildRows();
    void updateBar();
    void updateBundles();
    void openCategory(int category); // -1: all
    [[nodiscard]] int everythingEntry() const { return static_cast<int>(m_categories.size()) + 1; }
    [[nodiscard]] bool searching() const { return m_needle.size() >= 2; }
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void click(int row, int column, ui::PointF p);

    AppState& m_state;
    ProgramsController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_goToImages;
    std::size_t m_subscription = 0;
    std::vector<ProgramsController::Category> m_categories;
    std::vector<ProgramsController::Bundle> m_bundles;
    std::vector<Row> m_rows;
    std::wstring m_needle;
    int m_categoryFilter = 0; // 0 = all, else position in m_categories + 1; everythingEntry(): the repository
    bool m_onlyPicked = false;

    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_category = nullptr;
    ui::Toggle* m_pickedOnly = nullptr;
    BundleStrip* m_bundleStrip = nullptr;
    ui::InfoBar* m_bar = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
