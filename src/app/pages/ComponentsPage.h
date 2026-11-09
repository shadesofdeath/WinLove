#pragma once
// P07 Bileşenler as the reference tool's component tree (D-110, D-112; research README §6): the fourteen
// categories, the nodes under them (a driver class, "Dil", "Klavye düzenleri", "Yazı Tipleri",
// "Tek tek cihazlar [Gelişmiş]" …), the components, and what a component shows under it (a driver's
// devices). One 23.5 row each — chevron, the check circle with its template arc (tier.*), the name;
// "Boyut (MB)" as a logarithmic bar with the number on it. A tick means KEPT: unticking queues the
// removal, as in the reference tool; a category's or a node's circle takes its components together. Rows held
// by a guard or locked for good are grey; "not recommended" names are dimmed. Filter row: "Tümü ▾"
// and the search box. What a row is (notes, size, template, why it is locked) is its tooltip.
#include "app/Localization.h"
#include "app/controllers/ComponentController.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <functional>
#include <set>

namespace wl::app {

class ComponentsPage : public ui::Widget {
public:
    void refresh(); // the groups again (also when the compatibility guards changed, D-082)
    ComponentsPage(AppState& state, ComponentController& controller, const Localization& strings, Language language,
                   std::function<void()> goToImages);
    ~ComponentsPage() override;

    // Selection (nullopt: not a component row).
    std::function<void()> onSelectionChanged;
    [[nodiscard]] std::optional<ComponentController::Item> selectedItem() const;
    [[nodiscard]] std::wstring selectedGroupName() const;
    // D-082: why a row is held (the shell knows the guards' names); empty = not held.
    std::function<std::wstring(const ComponentController::Item&)> explainBlock;

    void focusSearch();
    // Command palette: drops the filters, opens the component's category and nodes, selects it.
    void reveal(const std::wstring& packageName);
    void setAllExpanded(bool expanded);
    [[nodiscard]] bool allExpanded() const;

    // The filter of "Tümü ▾", in its order.
    enum class Filter : std::uint8_t { All, Kept, Removed, UserApps, SystemApps, Drivers, Privacy, General, Light };
    void setFilter(Filter filter);

    void layout() override;
    void paint(ui::Canvas& canvas) override;
    bool onChar(wchar_t ch) override;

private:
    struct Row {
        enum class Kind : std::uint8_t { Category, Node, Item, Child };
        Kind kind = Kind::Category;
        int group = 0;     // index into m_groups
        int item = -1;     // Item / Child: index into the group's items
        int depth = 0;     // indent level
        std::wstring key;  // Category / Node / Item with children: its expansion key
        std::wstring text; // Node: its name; Child: its line
    };
    void rebuildRows();
    void emit(int group, const std::vector<int>& items, std::vector<std::wstring>& prefix, int depth, bool open);
    // D-113: one component row and, when it is open, the components that depend on it and its lines.
    void emitItem(int group, const std::vector<int>& items, int item, int depth, bool open);
    void updateRiskBar();
    [[nodiscard]] bool itemVisible(const ComponentController::Item& item) const;
    [[nodiscard]] bool filtering() const;
    [[nodiscard]] bool expanded(const std::wstring& key) const;
    // The components a category / node row takes (visible or not): indexes into its group.
    [[nodiscard]] std::vector<int> itemsUnder(const Row& row) const;
    // Each row's circle (category / node: from its components) and whether nothing in it can change;
    // computed when the rows or the queue change, not per paint.
    void updateChecks();
    struct CheckState {
        std::uint8_t state = 2; // TreeCheck
        bool held = false;
    };
    std::vector<CheckState> m_checks;
    [[nodiscard]] std::wstring tooltipOf(int row) const;
    void paintCell(ui::Canvas& canvas, int row, int column, ui::RectF rect, ui::TableView::CellState cell);
    void click(int row, int column, ui::PointF p);
    void toggleRow(int row);

    AppState& m_state;
    ComponentController& m_controller;
    const Localization& m_strings;
    Language m_language;
    std::function<void()> m_goToImages;
    std::size_t m_subscription = 0;
    std::vector<ComponentController::Group> m_groups;
    std::vector<Row> m_rows;
    std::set<std::wstring> m_expanded; // keys of open categories, nodes and components (the reference tool opens closed)
    std::wstring m_needle;
    Filter m_filter = Filter::All;
    float m_nameWidth = 380.0f;

    ui::Dropdown* m_filterBox = nullptr;
    ui::SearchBox* m_search = nullptr;
    ui::InfoBar* m_riskBar = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
