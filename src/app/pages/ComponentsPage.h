#pragma once
// P07 Bileşenler (docs/pages/07-components.md, screens 04 / 04b): provisioned apps as a two-level
// tri-state tree (catalog group → app) on a TableView — Ad · Risk · Boyut. Toolbar: search ("/",
// matches highlighted, only matching branches), Kategori, Risk, "Yalnızca seçili"; the risk
// InfoBar names the irreversible picks. The selected app goes to the Shell's inspector column.
#include "app/Localization.h"
#include "app/controllers/ComponentController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <functional>
#include <set>

namespace wl::app {

class ComponentsPage : public ui::Widget {
public:
    ComponentsPage(AppState& state, ComponentController& controller, const Localization& strings, Language language,
                   std::function<void()> goToImages);
    ~ComponentsPage() override;

    // Selection for the inspector (nullopt: a group row or nothing).
    std::function<void()> onSelectionChanged;
    [[nodiscard]] std::optional<ComponentController::Item> selectedItem() const;
    [[nodiscard]] std::wstring selectedGroupName() const;

    void focusSearch();
    void setAllExpanded(bool expanded);
    [[nodiscard]] bool allExpanded() const;

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    struct Row {
        int group = 0;   // index into m_groups
        int item = -1;   // index into the group's items, -1 = the group row
        int matches = 0; // group rows while searching: matching items
    };
    void refresh();
    void rebuildRows();
    void updateRiskBar();
    [[nodiscard]] bool itemVisible(const ComponentController::Item& item) const;
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void click(int row, int column, ui::PointF p);

    AppState& m_state;
    ComponentController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_goToImages;
    std::size_t m_subscription = 0;
    std::vector<ComponentController::Group> m_groups;
    std::vector<Row> m_rows;
    std::set<int> m_collapsed; // catalog indexes of collapsed groups
    std::wstring m_needle;
    int m_categoryFilter = 0; // 0 = all, else group catalogIndex + 1
    int m_riskFilter = 0;     // 0 all, 1 low, 2 medium, 3 high
    bool m_onlySelected = false;

    ui::SearchBox* m_search = nullptr;
    ui::Dropdown* m_category = nullptr;
    ui::Dropdown* m_risk = nullptr;
    ui::Toggle* m_selectedOnly = nullptr;
    ui::InfoBar* m_riskBar = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
