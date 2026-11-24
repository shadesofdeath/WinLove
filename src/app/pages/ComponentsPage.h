#pragma once
// P07 Bileşenler as the reference tool's component tree (D-110, D-112; research README §6): the fourteen
// categories, the nodes under them (a driver class, "Dil", "Klavye düzenleri", "Yazı Tipleri",
// "Tek tek cihazlar [Gelişmiş]" …), the components, and what a component shows under it (a driver's
// devices). One 23.5 row each — chevron, the check circle with its template arc (tier.*), the name;
// "Boyut (MB)" as a logarithmic bar with the number on it. A tick means KEPT: unticking queues the
// removal, as in the reference tool; a category's or a node's circle takes its components together. Rows held
// by a guard or locked for good are grey; "not recommended" names are dimmed. Filter row: "Tümü ▾"
// and the search box. What a row is (notes, size, template, why it is locked) is its tooltip.
// D-139: the header row says what the image has and what Uygula removes ("1.312 bileşen · 9,80 GB ·
// 13 kaldırılacak · 1,05 GB"); the column heads are the table's own; a narrow marks column after the
// name (queued / held / locked, the catalog's risk); the selected row in the detail card.
#include "app/Localization.h"
#include "app/controllers/ComponentController.h"
#include "app/pages/DetailCard.h"
#include "app/pages/PageBits.h"
#include "app/pages/PageHeader.h"
#include "ui/widgets/Dropdown.h"
#include "ui/widgets/EmptyState.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TableView.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wl::app {

class ComponentsPage : public ui::Widget, public DetailSource {
public:
    void refresh(); // the groups again (also when the compatibility guards changed, D-082)
    // Tests (2026-11-10 polish): what a row's name cell says at its right ("12/40") and in the
    // size column, and where the size column's head is drawn against its numbers.
    [[nodiscard]] int rowCount() const noexcept { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] std::wstring rowCountText(int row) const;
    [[nodiscard]] std::wstring rowSizeText(int row) const;
    [[nodiscard]] ui::RectF sizeHead() const noexcept { return m_sizeHead; }
    [[nodiscard]] float sizeNumberX() const; // where a row's size number starts
    [[nodiscard]] std::wstring summaryText() const { return m_summary->text(); } // D-139 tests
    ComponentsPage(AppState& state, ComponentController& controller, const Localization& strings, Language language,
                   std::function<void()> goToImages);
    ~ComponentsPage() override;

    // Selection (nullopt: not a component row).
    std::function<void()> onSelectionChanged;
    [[nodiscard]] std::optional<ComponentController::Item> selectedItem() const;
    [[nodiscard]] std::wstring selectedGroupName() const;
    // D-082: why a row is held (the shell knows the guards' names); empty = not held.
    std::function<std::wstring(const ComponentController::Item&)> explainBlock;
    // D-153: what removing the item also fixes (RemovalGaps), one line each; empty: nothing.
    std::function<std::vector<std::wstring>(const ComponentController::Item&)> gapsOf;

    void focusSearch();
    // Command palette: drops the filters, opens the component's category and nodes, selects it.
    void reveal(const std::wstring& packageName);
    void setAllExpanded(bool expanded);
    [[nodiscard]] bool allExpanded() const;

    // The filter of "Tümü ▾", in its order.
    enum class Filter : std::uint8_t { All, Kept, Removed, UserApps, SystemApps, Drivers, Privacy, General, Light };
    void setFilter(Filter filter);

    // D-139: the detail card.
    enum Action : int { kActionToggle = 1 };
    [[nodiscard]] DetailModel detail() const override;
    [[nodiscard]] bool detailAvailable() const override { return m_table->visible(); }
    void detailAction(int id) override;

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
        // Item, filled by rebuildRows() so paint does not fold / format per frame (audit PG-19):
        std::size_t matchAt = std::wstring::npos; // where the search text is in the name
        std::wstring sizeText;                    // "87.93" (MB) on the size bar
    };
    void rebuildRows();
    // What the selected row is (its expansion / package key), read against the rows and groups as they
    // are: refresh() asks before it takes new groups (the old rows index the old ones).
    [[nodiscard]] std::wstring selectedKey() const;
    std::wstring m_keepKey; // refresh(): the key rebuildRows() finds again
    bool m_keepKnown = false;
    void emit(int group, const std::vector<int>& items, std::vector<std::wstring>& prefix, int depth, bool open);
    // D-113: one component row and, when it is open, the components that depend on it and its lines.
    void emitItem(int group, const std::vector<int>& items, int item, int depth, bool open);
    void updateRiskBar();
    void updateSummary(); // the header row's line (refresh, queue changes; never paint)
    void selectionChanged();
    [[nodiscard]] RowMarks marksOf(int row) const;
    [[nodiscard]] std::wstring marksTip(int row) const;
    [[nodiscard]] bool itemVisible(const ComponentController::Item& item) const;
    [[nodiscard]] bool filtering() const;
    [[nodiscard]] bool expanded(const std::wstring& key) const;
    // The components a category / node row takes (visible or not): indexes into its group.
    [[nodiscard]] std::vector<int> itemsUnder(const Row& row) const;
    // Each row's circle (category / node: from its components) and whether nothing in it can change;
    // a category / node row also what is under it: "kept/all" and the components' total size (MB,
    // as the size column writes it). Computed when the rows or the queue change, not per paint.
    void updateChecks();
    struct CheckState {
        std::uint8_t state = 2; // TreeCheck
        bool held = false;
        std::wstring count;     // category / node: "12/40" (ticked = kept, of all under it)
        std::wstring sizeText;  // category / node: "1234.56"
    };
    std::vector<CheckState> m_checks;
    // The size column's head ("Boyut (MB)") in the filter row, at the x of the numbers under it
    // (layout(): the name column's width follows the page's).
    ui::RectF m_sizeHead{};
    void toggleByUser(int row); // toggleRow() + the clicked row's tick drawing in
    // Audit PG-02: whether a component is queued / held by a guard, asked once per state change.
    // A category row counts every component of its group and each node row its own again; opening
    // a node or typing in the search box rebuilt the rows and asked the guards ~1,300 times anew.
    struct ItemFlags {
        std::int8_t queued = -1; // -1: not asked yet
        std::int8_t held = -1;
    };
    void syncItemFlags() const;
    [[nodiscard]] bool itemQueued(std::size_t group, std::size_t item) const;
    [[nodiscard]] bool itemHeld(std::size_t group, std::size_t item) const; // block() is not empty
    static constexpr std::uint64_t kNoRevision = ~std::uint64_t{0};
    mutable std::vector<std::vector<ItemFlags>> m_itemFlags; // [group][item]
    mutable std::uint64_t m_itemFlagsAt = kNoRevision;       // AppState::revision() they are good for
    // One category's visible components while rebuildRows() emits it: their names, and who hangs
    // under whom (D-113) - emit() built the set at every level and emitItem() scanned the list per row.
    std::set<std::wstring> m_emitShown;
    std::map<std::wstring, std::vector<int>> m_emitDependents; // parentKey -> items, in list order
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
    SummaryLine* m_summary = nullptr;
    ui::TableView* m_table = nullptr;
    ui::EmptyState* m_empty = nullptr;
};

} // namespace wl::app
