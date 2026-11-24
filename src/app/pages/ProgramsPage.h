#pragma once
// D-078 Programlar: winget programs the installed Windows gets at the first sign-in. Toolbar:
// search over the whole repository ("/"), Kategori, "Yalnızca seçili", and on the right how many
// are picked. Under it the bundles (BundleStrip): a click picks a bundle's programs. Before a
// search the catalog's categories are listed (programs.json) — each with its featured programs and
// a "N program daha" row that opens the category: then its featured programs, then every package
// winget tags as it. Kategori's last entry is the whole repository. The picks from searches come at
// the end; a search lists the repository's best matches. Table: Program (box, icon, name) · Kimlik
// · Sürüm, 24px rows. The selected program goes to the Shell's inspector column (ProgramInspector).
// D-139: that column is the shared detail card now (the same head, facts, sections and quiet buttons as
// every working page; "Bir satır seç" when nothing is); the toolbar's right says what is picked in the
// header row's style (SummaryLine, made on the notifications — it was formatted on every frame).
#include "app/Localization.h"
#include "app/controllers/ProgramsController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageHeader.h"
#include "app/pages/programs/BundleStrip.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace wl::app {

class ProgramsPage : public ui::Widget, public DetailSource {
public:
    ProgramsPage(AppState& state, ProgramsController& controller, const Localization& strings, Language language,
                 std::function<void()> goToImages);
    ~ProgramsPage() override;

    std::function<void()> onSelectionChanged;
    [[nodiscard]] std::optional<core::WingetPackage> selectedProgram() const;
    void focusSearch();
    // A manifest's https address (the card's "Ana sayfa"): the shell opens it.
    std::function<void(const std::wstring& url)> openUrl;

    // D-139: the detail card.
    enum Action : int { kActionToggle = 1, kActionHomepage };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;
    [[nodiscard]] std::wstring summaryText() const { return m_summaryLine->text(); } // tests

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
        // A program row, worked out the first time it is drawn (audit PG-06: per row per frame the
        // picks were searched and the name and the search text folded). The mark holds for the
        // row's life (a new search text makes new rows), the check box until the state changes.
        std::uint64_t pickedAt = ~std::uint64_t{0}; // AppState::revision() `picked` is good for
        bool picked = false;
        bool matchKnown = false;
        std::size_t matchAt = std::wstring::npos; // where the search text is in the name
    };
    void refresh();
    void rebuildRows();
    void selectionChanged();
    void updateSummary();
    void updateBar();
    void updateBundles();
    void openCategory(int category); // -1: all
    [[nodiscard]] int everythingEntry() const { return static_cast<int>(m_categories.size()) + 1; }
    [[nodiscard]] bool searching() const { return m_needle.size() >= 2; }
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void click(int row, int column, ui::PointF p);
    void togglePick(int row); // the user's click / Enter on a program: its tick draws in

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
    SummaryLine* m_summaryLine = nullptr;
    BundleStrip* m_bundleStrip = nullptr;
    ui::InfoBar* m_bar = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
