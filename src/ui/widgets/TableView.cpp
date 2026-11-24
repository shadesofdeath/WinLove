#include "ui/widgets/TableView.h"

#include "ui/widgets/Checkbox.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace wl::ui {

namespace {
using tokens::Color;
using tokens::TypeStyle;
constexpr float kSortArrow = 10.0f;

int compareKeys(const TableSortKey& a, const TableSortKey& b) {
    if (!a.text.empty() || !b.text.empty()) {
        const int r = CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, a.text.data(),
                                      static_cast<int>(a.text.size()), b.text.data(), static_cast<int>(b.text.size()),
                                      nullptr, nullptr, 0);
        if (r != CSTR_EQUAL && r != 0) {
            return r == CSTR_LESS_THAN ? -1 : 1;
        }
    }
    return a.number < b.number ? -1 : a.number > b.number ? 1 : 0;
}
} // namespace

int TableView::toData(int visual) const noexcept {
    return visual < 0 || m_order.empty() || visual >= static_cast<int>(m_order.size()) ? visual
                                                                                         : m_order[static_cast<std::size_t>(visual)];
}

int TableView::toVisual(int data) const noexcept {
    return data < 0 || m_inverse.empty() || data >= static_cast<int>(m_inverse.size()) ? data
                                                                                         : m_inverse[static_cast<std::size_t>(data)];
}

void TableView::sortRows() {
    const int selectedData = toData(m_selected);
    m_order.clear();
    m_inverse.clear();
    if (m_sortColumn >= 0 && sortKey && m_count > 1) {
        std::vector<TableSortKey> keys;
        keys.reserve(static_cast<std::size_t>(m_count));
        for (int row = 0; row < m_count; ++row) {
            keys.push_back(sortKey(row, m_sortColumn));
        }
        m_order.resize(static_cast<std::size_t>(m_count));
        std::iota(m_order.begin(), m_order.end(), 0);
        std::ranges::stable_sort(m_order, [&](int a, int b) {
            const int c = compareKeys(keys[static_cast<std::size_t>(a)], keys[static_cast<std::size_t>(b)]);
            return m_sortAscending ? c < 0 : c > 0;
        });
        m_inverse.resize(m_order.size());
        for (std::size_t visual = 0; visual < m_order.size(); ++visual) {
            m_inverse[static_cast<std::size_t>(m_order[visual])] = static_cast<int>(visual);
        }
    }
    m_selected = toVisual(selectedData);
}

void TableView::refresh() {
    if (m_sortColumn >= 0) {
        sortRows();
    }
    invalidate();
}

TableView::TableView(std::vector<TableColumn> columns) : m_columns(std::move(columns)) {
    setFocusable(true);
    setAccessible(AccessRole::List, L"");
    m_scrollBar = &add<ScrollBar>();
    m_scrollBar->onScroll = [this](float offset) {
        m_offset = offset;
        invalidate();
    };
}

RectF TableView::body() const {
    const RectF b = bounds();
    return {b.x, b.y + m_header, b.width, std::max(b.height - m_header, 0.0f)};
}

std::vector<float> TableView::columnXs() const {
    std::vector<float> xs;
    columnXs(xs);
    return xs;
}

void TableView::columnXs(std::vector<float>& xs) const {
    xs.clear();
    const RectF b = bounds();
    float fixed = 0;
    float give = 0; // what the fixed columns with a minWidth can give up
    bool hasFlex = false;
    for (const auto& c : m_columns) {
        if (c.hidden) {
            continue;
        }
        fixed += c.width;
        hasFlex = hasFlex || c.width <= 0;
        if (c.width > 0 && c.minWidth > 0) {
            give += std::max(c.width - c.minWidth, 0.0f);
        }
    }
    // D-136: a narrow table keeps its flexible (name) column at m_flexMin as long as the shrinkable
    // fixed columns have room to give, each in proportion to what it can give.
    const float wanted = hasFlex ? std::max(m_flexMin - (b.width - fixed), 0.0f) : 0.0f;
    const float taken = std::min(wanted, give);
    const float flex = std::max(b.width - fixed + taken, kFlexMin);
    float x = b.x;
    for (const auto& c : m_columns) {
        xs.push_back(x);
        if (c.hidden) {
            continue; // no width: its left edge is the next one's
        }
        float w = c.width > 0 ? c.width : flex;
        if (c.width > 0 && c.minWidth > 0 && give > 0) {
            w -= taken * std::max(c.width - c.minWidth, 0.0f) / give;
        }
        x += w;
    }
    xs.push_back(x);
}

float TableView::minimumWidth() const noexcept {
    float width = 0;
    for (const auto& c : m_columns) {
        if (!c.hidden) {
            width += c.width <= 0 ? m_flexMin : c.minWidth > 0 ? c.minWidth : c.width;
        }
    }
    return width;
}

void TableView::layout() {
    const RectF v = body();
    m_scrollBar->setBounds({v.right() - ScrollBar::kWidth, v.y, ScrollBar::kWidth, v.height});
    m_scrollBar->setRange(static_cast<float>(m_count) * m_row, v.height);
    m_offset = std::clamp(m_offset, 0.0f, m_scrollBar->maxOffset());
    m_scrollBar->setOffset(m_offset);
}

void TableView::setRowCount(int count) {
    const int selectedData = toData(m_selected);
    m_count = std::max(count, 0);
    m_order.clear();
    m_inverse.clear();
    m_selected = selectedData >= m_count ? (m_count > 0 ? m_count - 1 : -1) : selectedData;
    if (m_sortColumn >= 0) {
        sortRows();
    }
    m_hoverRow = -1;
    m_hoverFade.snap(-1); // other rows now: nothing of the old highlight fades out over them
    layout();
    invalidate();
}

void TableView::revealCheck(int row) {
    m_revealRow = row;
    m_reveal.snapTo(0.0f);
    if (m_reveal.animateTo(1.0f, Checkbox::kRevealMs)) {
        animate();
    }
    invalidate();
}

bool TableView::tick(double now) {
    const bool hover = m_hoverFade.tick(now);
    const bool reveal = m_reveal.tick(now);
    invalidate();
    return hover || reveal;
}

void TableView::setHoverRow(int row) {
    m_hoverRow = row;
    if (m_hoverFade.set(row)) {
        animate();
    }
}

void TableView::clearSelection() {
    m_selected = -1;
    invalidate();
}

void TableView::setSelected(int row, bool reveal) {
    select(toVisual(m_count > 0 ? std::clamp(row, 0, m_count - 1) : -1), reveal);
}

void TableView::select(int row, bool reveal) {
    row = m_count > 0 ? std::clamp(row, 0, m_count - 1) : -1;
    if (row != m_selected) {
        m_selected = row;
        if (onSelect && row >= 0) {
            onSelect(toData(row));
        }
    }
    if (reveal && row >= 0) {
        this->reveal(row);
    }
    invalidate();
}

RectF TableView::cellRect(int row, int column) const {
    row = toVisual(row);
    const auto xs = columnXs();
    const RectF v = body();
    const float y = std::round(v.y + static_cast<float>(row) * m_row - m_offset);
    return {xs[static_cast<std::size_t>(column)], y, xs[static_cast<std::size_t>(column) + 1] - xs[static_cast<std::size_t>(column)],
            m_row};
}

RectF TableView::focusRect() const {
    // The Host draws its ring outside this rectangle (focusOffset + 1 out, 2 DIP wide, inwards):
    // inset by that much, the ring lies on the selected row's own edge, as painted (rounded y).
    // An outside ring was cut off by the card around a full-width table and covered half of the
    // rows above and below; with nothing selected it went round the whole table, of which only
    // the top line showed.
    const float ring = tokens::radius::focusOffset + 1;
    const RectF v = body();
    if (m_selected >= 0) {
        const float y = std::round(v.y + static_cast<float>(m_selected) * m_row - m_offset);
        if (y >= v.y - 1 && y + m_row <= v.bottom() + 1) {
            return RectF{v.x, y, v.width, m_row}.inset(ring, ring);
        }
    }
    return v.width > 2 * ring && v.height > 2 * ring ? v.inset(ring, ring) : bounds();
}

void TableView::setOffset(float offset) {
    m_offset = std::clamp(offset, 0.0f, m_scrollBar->maxOffset());
    m_scrollBar->setOffset(m_offset);
    invalidate();
}

void TableView::reveal(int row) {
    const RectF v = body();
    const float top = static_cast<float>(row) * m_row;
    if (top < m_offset) {
        setOffset(top);
    } else if (top + m_row > m_offset + v.height) {
        setOffset(top + m_row - v.height);
    }
}

bool TableView::onWheel(PointF p, float lines) {
    if (!m_scrollBar->needed()) {
        return false;
    }
    setOffset(m_offset - lines * m_row);
    onPointerMove(p); // no mouse-move follows a wheel: the row under the pointer changed
    return true;
}

int TableView::rowAt(PointF p) const {
    const RectF v = body();
    if (!v.contains(p)) {
        return -1;
    }
    const int row = static_cast<int>(std::floor((p.y - v.y + m_offset) / m_row));
    return row >= 0 && row < m_count ? row : -1;
}

int TableView::columnAt(float x) const {
    const auto xs = columnXs();
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        if (x >= xs[i] && x < xs[i + 1]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TableView::onPointerMove(PointF p) {
    const int row = rowAt(p);
    const int column = row >= 0 ? columnAt(p.x) : -1;
    const RectF b = bounds();
    int header = p.y >= b.y && p.y < b.y + m_header ? columnAt(p.x) : -1;
    if (header >= 0 && !m_columns[static_cast<std::size_t>(header)].sortable) {
        header = -1;
    }
    if (row != m_hoverRow || ((cellText || cellTooltip) && column != m_hoverColumn)) {
        updateTooltip(row, column);
    }
    if (row != m_hoverRow || column != m_hoverColumn || header != m_hoverHeader) {
        setHoverRow(row);
        m_hoverColumn = column;
        m_hoverHeader = header;
        invalidate();
    }
}

void TableView::updateTooltip(int row, int column) {
    // D-110: a row says what it is; D-136: else a cell whose text does not fit says it whole.
    std::wstring tip = row >= 0 && column >= 0 && cellTooltip ? cellTooltip(toData(row), column) : std::wstring{};
    if (tip.empty() && row >= 0 && tooltipFor) {
        tip = tooltipFor(toData(row));
    }
    if (tip.empty() && row >= 0 && column >= 0 && cellText) {
        TableCellText cell = cellText(toData(row), column);
        const auto xs = columnXs();
        const float room = xs[static_cast<std::size_t>(column) + 1] - xs[static_cast<std::size_t>(column)] - 2 * kCellPad - cell.inset;
        if (!cell.text.empty() && textWidth(cell.text, cell.style) > room + 0.5f) {
            tip = std::move(cell.text);
        }
    }
    if (tip != tooltip()) {
        setTooltip(std::move(tip));
    }
}

void TableView::onHoverChanged(bool hovered) {
    if (!hovered) {
        setHoverRow(-1);
        m_hoverColumn = m_hoverHeader = -1;
    }
    invalidate();
}

void TableView::onPointerDown(PointF p) {
    const RectF b = bounds();
    if (p.y >= b.y && p.y < b.y + m_header && sortKey) {
        // A sortable header: ascending, descending, then the owner's order again.
        const int column = columnAt(p.x);
        if (column >= 0 && m_columns[static_cast<std::size_t>(column)].sortable) {
            if (column != m_sortColumn) {
                m_sortColumn = column;
                m_sortAscending = true;
            } else if (m_sortAscending) {
                m_sortAscending = false;
            } else {
                m_sortColumn = -1;
            }
            sortRows();
            if (m_sortColumn < 0) {
                m_order.clear();
                m_inverse.clear();
            }
            invalidate();
        }
        m_downRow = -1;
        return;
    }
    m_downRow = rowAt(p);
    m_downPoint = p;
    m_downColumn = m_downRow >= 0 ? columnAt(p.x) : -1;
    setHoverRow(m_downRow); // the press point is authoritative even if hover is stale
    m_hoverColumn = m_downColumn;
    if (m_downRow >= 0) {
        select(m_downRow, /*reveal=*/false);
    }
}

void TableView::onClick() {
    m_previousClickRow = m_clickRow;
    m_clickRow = m_downRow;
    if (m_downRow >= 0 && m_downRow == m_hoverRow && onCellClick) {
        onCellClick(toData(m_downRow), m_downColumn, m_downPoint);
    }
}

void TableView::onDoubleClick() {
    // Both clicks must land on the same row.
    if (m_downRow >= 0 && m_downRow == m_previousClickRow && onActivate) {
        onActivate(toData(m_downRow));
    }
}

bool TableView::onKeyDown(const KeyEvent& key) {
    if (onKey && onKey(key)) {
        return true;
    }
    if (m_count == 0) {
        return false;
    }
    const int page = std::max(static_cast<int>(body().height / m_row) - 1, 1);
    switch (key.virtualKey) {
    case VK_DOWN: select(m_selected < 0 ? 0 : m_selected + 1, true); return true;
    case VK_UP: select(m_selected < 0 ? 0 : m_selected - 1, true); return true;
    case VK_NEXT: select(m_selected + page, true); return true;
    case VK_PRIOR: select(m_selected - page, true); return true;
    case VK_HOME: select(0, true); return true;
    case VK_END: select(m_count - 1, true); return true;
    case VK_RETURN:
    case VK_SPACE:
        if (m_selected >= 0 && onActivate) {
            onActivate(toData(m_selected));
            return true;
        }
        return false;
    default: return false;
    }
}

void TableView::paint(Canvas& canvas) {
    const RectF b = bounds();
    columnXs(m_xs);
    const auto& xs = m_xs;
    // Header.
    for (std::size_t i = 0; i < m_columns.size(); ++i) {
        const auto& c = m_columns[i];
        if (c.hidden) {
            continue;
        }
        const RectF cell{xs[i] + kCellPad, b.y, xs[i + 1] - xs[i] - 2 * kCellPad, m_header};
        const bool sorted = static_cast<int>(i) == m_sortColumn;
        const Color ink = sorted || static_cast<int>(i) == m_hoverHeader ? Color::TextSecondary : Color::TextTertiary;
        if (sorted) {
            // The arrow beside the title, on the side the text does not grow towards.
            const float w = std::min(std::ceil(textWidth(c.title, TypeStyle::Caption, 40.0f)), cell.width - kSortArrow - 4);
            const float ax = c.align == TextAlign::Trailing ? cell.right() - w - 4 - kSortArrow : cell.x + w + 4;
            canvas.drawIcon(m_sortAscending ? icons::Icon::ArrowUp : icons::Icon::ArrowDown,
                            {ax, cell.y + (m_header - kSortArrow) / 2}, Color::AccentBase, IconVariant::Regular16, kSortArrow);
        }
        canvas.drawText(c.title, cell, TypeStyle::Caption, ink, c.align);
    }
    if (m_header > 0) {
        canvas.hairlineH(b.x, b.y + m_header - (1.0f / canvas.scale()), b.width, Color::LineSubtle);
    }

    const RectF v = body();
    canvas.pushClip(v);
    const int first = std::max(static_cast<int>(m_offset / m_row), 0);
    const int last = std::min(static_cast<int>((m_offset + v.height) / m_row) + 1, m_count - 1);
    for (int row = first; row <= last; ++row) {
        const float y = std::round(v.y + static_cast<float>(row) * m_row - m_offset);
        const RectF line{v.x, y, v.width, m_row};
        const bool selected = row == m_selected;
        const bool hovered = row == m_hoverRow;
        // bg.hover, faded in and out like a Button's: 1 for the hovered row at rest, so a still
        // frame is the plain fill.
        const float hover = m_hoverFade.amount(row);
        if (selected) { // Painter::RowBackground: acSoft + a 2px accent bar inset 10 (less on short rows)
            canvas.fillRect(line, Color::AccentSubtle);
            const float inset = std::min(10.0f, m_row / 4);
            canvas.fillRoundRect({line.x, y + inset, 2.0f, m_row - 2 * inset}, 1.0f, Color::AccentBase);
        } else if (hover > 0) {
            canvas.fillRect(line, Ink(Color::BgHover, Color::BgHover, 0.0f, hover));
        }
        // D-109: the reference tool's grids have no lines between rows.
        if (paintCell) {
            const int data = toData(row);
            const float reveal = m_reveal.running() && data == m_revealRow ? m_reveal.value() : 1.0f;
            for (std::size_t i = 0; i < m_columns.size(); ++i) {
                if (m_columns[i].hidden) {
                    continue;
                }
                const RectF cell{xs[i] + kCellPad, y, xs[i + 1] - xs[i] - 2 * kCellPad, m_row};
                paintCell(canvas, data, static_cast<int>(i), cell,
                          CellState{hovered, selected, hovered && static_cast<int>(i) == m_hoverColumn, reveal});
            }
        }
    }
    canvas.popClip();
}

} // namespace wl::ui
