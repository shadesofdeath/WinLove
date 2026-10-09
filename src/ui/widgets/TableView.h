#pragma once
// DataGrid-lite per 03_components/datagrid.md: 24px header (11px text.tertiary, 1px line.subtle
// below), 24px rows with 1px line.subtle separators, hover bg.raised, selected accent.subtle,
// virtual scrolling with the overlay ScrollBar, single selection with ↑↓ PgUp/PgDn Home/End.
// The owner supplies the row count and paints cells (it knows its data); the table owns
// geometry, scrolling, selection and input. D-091: sorting — a sortable column's header cycles
// ascending / descending / off; the owner gives a key per cell (sortKey) and keeps talking in its
// own row numbers (every callback and setter here takes the owner's rows, not the sorted order).
// Not yet: column resize, multi-select.
#include "ui/widget/Widget.h"
#include "ui/widgets/ScrollBar.h"

#include <functional>
#include <string>
#include <vector>

namespace wl::ui {

struct TableColumn {
    std::wstring title;
    float width = 0;          // 0 = takes the remaining space (one flex column)
    TextAlign align = TextAlign::Leading;
    bool sortable = false;    // needs TableView::sortKey
};

// A cell's sort key: text (compared as people read it: Turkish order, case-blind, digits as
// numbers) when not empty, else the number.
struct TableSortKey {
    std::wstring text;
    double number = 0;
};

class TableView : public Widget {
public:
    static constexpr float kHeader = 24.0f;
    static constexpr float kRow = 24.0f;
    static constexpr float kCellPad = 8.0f;

    explicit TableView(std::vector<TableColumn> columns);

    struct CellState {
        bool hovered;
        bool selected;
        bool hoveredCell; // pointer over this very cell (e.g. a toggle)
    };
    // Paint one cell's content inside `rect` (padding already applied).
    std::function<void(Canvas&, int row, int column, RectF rect, CellState)> paintCell;
    std::function<void(int row)> onSelect;
    std::function<void(int row, int column, PointF p)> onCellClick; // single click; p = press point
    std::function<void(int row)> onActivate;              // Enter / double click / Space
    std::function<bool(const KeyEvent&)> onKey;           // runs first (e.g. Delete removes a row)
    std::function<TableSortKey(int row, int column)> sortKey;

    void setRowCount(int count); // keeps the selection when still in range
    [[nodiscard]] int rowCount() const noexcept { return m_count; }
    void setSelected(int row, bool reveal = true);
    void clearSelection(); // no row selected (rows were rebuilt and the old one is gone)
    void scrollToTop() { setOffset(0); } // other rows than before: shown from the first
    [[nodiscard]] int selected() const noexcept { return toData(m_selected); }
    void refresh(); // data changed, same rows (sorted again)

    [[nodiscard]] RectF cellRect(int row, int column) const; // full cell (no padding)

    void layout() override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    [[nodiscard]] RectF focusRect() const override;
    bool onWheel(PointF p, float lines) override;
    void onPointerMove(PointF p) override;
    void onHoverChanged(bool hovered) override;
    void onPointerDown(PointF p) override;
    void onClick() override;
    void onDoubleClick() override;
    bool onKeyDown(const KeyEvent& key) override;

private:
    [[nodiscard]] int toData(int visual) const noexcept;
    [[nodiscard]] int toVisual(int data) const noexcept;
    void sortRows();       // m_order from sortKey, keeping the selected row selected
    void select(int visual, bool reveal);
    [[nodiscard]] RectF body() const;
    [[nodiscard]] std::vector<float> columnXs() const; // left edge of each column + right end
    [[nodiscard]] int rowAt(PointF p) const;
    [[nodiscard]] int columnAt(float x) const;
    void setOffset(float offset);
    void reveal(int row);

    std::vector<TableColumn> m_columns;
    int m_count = 0;
    int m_selected = -1;
    int m_hoverRow = -1;
    int m_hoverColumn = -1;
    int m_downRow = -1;
    int m_clickRow = -1;
    int m_previousClickRow = -1;
    int m_downColumn = -1;
    PointF m_downPoint{};
    float m_offset = 0;
    int m_sortColumn = -1;
    bool m_sortAscending = true;
    int m_hoverHeader = -1;
    std::vector<int> m_order;   // visual row -> owner's row (empty: the owner's order)
    std::vector<int> m_inverse; // owner's row -> visual row
    ScrollBar* m_scrollBar = nullptr;
};

} // namespace wl::ui
