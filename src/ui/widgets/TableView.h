#pragma once
// DataGrid-lite (D-107, the package's tables): 34px header (caption text.tertiary, 1px line below),
// 40px rows with 1px line separators, hover overlay, selected accent.subtle + 2px accent bar,
// virtual scrolling with the overlay ScrollBar, single selection with ↑↓ PgUp/PgDn Home/End.
// The owner supplies the row count and paints cells (it knows its data); the table owns
// geometry, scrolling, selection and input. D-091: sorting — a sortable column's header cycles
// ascending / descending / off; the owner gives a key per cell (sortKey) and keeps talking in its
// own row numbers (every callback and setter here takes the owner's rows, not the sorted order).
// Not yet: column resize, multi-select.
// Pointer and keyboard (D-109 has no lines between rows): the row under the pointer gets the
// bg.hover fill, faded like a Button's hover (ItemHover); keyboard focus is the Host's ring drawn
// just inside the selected row (inside the body when nothing is selected), so neither the card
// around the table nor the neighbouring rows cut or cover it.
#include "ui/anim/ItemMotion.h"
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
    // D-136: > 0 — a fixed column that gives up room (down to this) when the flexible column would
    // be narrower than the table's flexMin (a narrow window: the name column keeps its room).
    float minWidth = 0;
    // D-139: not shown (no width, no head, no cells): a page beside the detail card leaves out the
    // columns the card repeats for the selected row.
    bool hidden = false;
};

// What a cell says in full (TableView::cellText): shown as the cell's tooltip when it does not fit.
struct TableCellText {
    std::wstring text;
    tokens::TypeStyle style = tokens::TypeStyle::Body;
    float inset = 0; // where the text starts in the cell (after a check, an icon)
};

// A cell's sort key: text (compared as people read it: Turkish order, case-blind, digits as
// numbers) when not empty, else the number.
struct TableSortKey {
    std::wstring text;
    double number = 0;
};

class TableView : public Widget {
public:
    // D-107: the package's table (TableHeadH 34; one-line rows 40, the two-line edition rows are 52;
    // 12 between columns, 14 at the sides).
    static constexpr float kHeader = tokens::size::tableHead;
    static constexpr float kRow = tokens::size::row;
    static constexpr float kCellPad = 6.0f;
    static constexpr float kFlexMin = 120.0f; // the flexible column's least width (it overruns the table below it)

    explicit TableView(std::vector<TableColumn> columns);
    // D-107: a list of the package's (two-line rows of 56, no header) is a table with other heights.
    void setRowHeight(float height) { m_row = height; layout(); invalidate(); }
    void setHeaderHeight(float height) { m_header = height; layout(); invalidate(); }
    // D-110: a column's width after construction (the reference tool's grids keep the name column fixed).
    void setColumnWidth(int column, float width) {
        if (column >= 0 && column < static_cast<int>(m_columns.size())) {
            m_columns[static_cast<std::size_t>(column)].width = width;
            invalidate();
        }
    }
    // D-136: how wide the flexible column should stay; fixed columns with a minWidth shrink for it.
    void setFlexMin(float width) {
        m_flexMin = width;
        invalidate();
    }
    // D-139: leaves a column out (or brings it back); the others share its room.
    void setColumnHidden(int column, bool hidden) {
        if (column >= 0 && column < static_cast<int>(m_columns.size()) && m_columns[static_cast<std::size_t>(column)].hidden != hidden) {
            m_columns[static_cast<std::size_t>(column)].hidden = hidden;
            layout();
            invalidate();
        }
    }
    [[nodiscard]] bool columnHidden(int column) const noexcept {
        return column >= 0 && column < static_cast<int>(m_columns.size()) && m_columns[static_cast<std::size_t>(column)].hidden;
    }
    [[nodiscard]] int columnCount() const noexcept { return static_cast<int>(m_columns.size()); }
    // D-139: the least width the shown columns fit in (fixed ones at their minWidth, the flexible one
    // at the flexMin): below it a page beside the detail card leaves more columns out.
    [[nodiscard]] float minimumWidth() const noexcept;
    // The column edges at the table's width: left edge of each column, then the right end (tests).
    [[nodiscard]] std::vector<float> columnEdges() const { return columnXs(); }
    [[nodiscard]] float rowHeight() const noexcept { return m_row; }

    struct CellState {
        bool hovered;
        bool selected;
        bool hoveredCell; // pointer over this very cell (e.g. a toggle)
        // For a check drawn in the cell (Checkbox::paintBox's `reveal`): below 1 while the row's
        // tick draws itself in after revealCheck(); 1 otherwise.
        float checkReveal = 1.0f;
    };
    // Paint one cell's content inside `rect` (padding already applied).
    std::function<void(Canvas&, int row, int column, RectF rect, CellState)> paintCell;
    std::function<void(int row)> onSelect;
    std::function<void(int row, int column, PointF p)> onCellClick; // single click; p = press point
    std::function<void(int row)> onActivate;              // Enter / double click / Space
    std::function<bool(const KeyEvent&)> onKey;           // runs first (e.g. Delete removes a row)
    std::function<TableSortKey(int row, int column)> sortKey;
    std::function<std::wstring(int row)> tooltipFor; // D-110: the hovered row's tooltip (owner's row)
    // D-136: a cell's whole text (owner's row): when tooltipFor gives nothing and the text does not fit
    // its cell (it ends in "…"), the hovered cell's tooltip is the whole of it.
    std::function<TableCellText(int row, int column)> cellText;
    // D-139: a cell's own tooltip (owner's row), asked first — a state mark says what it means.
    std::function<std::wstring(int row, int column)> cellTooltip;

    void setRowCount(int count); // keeps the selection when still in range
    [[nodiscard]] int rowCount() const noexcept { return m_count; }
    void setSelected(int row, bool reveal = true);
    void clearSelection(); // no row selected (rows were rebuilt and the old one is gone)
    void scrollToTop() { setOffset(0); } // other rows than before: shown from the first
    [[nodiscard]] int selected() const noexcept { return toData(m_selected); }
    void refresh(); // data changed, same rows (sorted again)
    // The owner just ticked this row's check (owner's row): its CellState::checkReveal runs from 0
    // to 1, so a cell that passes it to Checkbox::paintBox draws the tick in.
    void revealCheck(int row);
    // The owner's row under the pointer, -1 when none (tooltips, tests).
    [[nodiscard]] int hoveredRow() const noexcept { return toData(m_hoverRow); }
    // A fade (hover) or a tick drawing in still needs frames.
    [[nodiscard]] bool animating() const noexcept { return m_hoverFade.running() || m_reveal.running(); }

    [[nodiscard]] RectF cellRect(int row, int column) const; // full cell (no padding)

    void layout() override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] bool clipsChildren() const noexcept override { return true; }
    [[nodiscard]] RectF focusRect() const override;
    [[nodiscard]] float focusRadius() const override { return 0.0f; } // rows are square
    bool tick(double now) override;
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
    void columnXs(std::vector<float>& out) const;     // into a kept buffer: paint() allocates nothing
    void setHoverRow(int row);                         // visual row; the fill fades over
    [[nodiscard]] int rowAt(PointF p) const;
    [[nodiscard]] int columnAt(float x) const;
    void setOffset(float offset);
    void reveal(int row);

    void updateTooltip(int row, int column); // visual row
    std::vector<TableColumn> m_columns;
    float m_flexMin = kFlexMin;
    float m_row = kRow;
    float m_header = kHeader;
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
    std::vector<float> m_xs;    // paint()'s column edges
    ItemHover m_hoverFade;      // by visual row, as m_hoverRow
    int m_revealRow = -1;       // owner's row whose tick draws in
    Tween m_reveal{1.0f};
    ScrollBar* m_scrollBar = nullptr;
};

} // namespace wl::ui
