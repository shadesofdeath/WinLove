#include "ui/widgets/TableView.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
using tokens::TypeStyle;
} // namespace

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
    return {b.x, b.y + kHeader, b.width, std::max(b.height - kHeader, 0.0f)};
}

std::vector<float> TableView::columnXs() const {
    const RectF b = bounds();
    float fixed = 0;
    for (const auto& c : m_columns) {
        fixed += c.width;
    }
    const float flex = std::max(b.width - fixed, 120.0f);
    std::vector<float> xs;
    float x = b.x;
    for (const auto& c : m_columns) {
        xs.push_back(x);
        x += c.width > 0 ? c.width : flex;
    }
    xs.push_back(x);
    return xs;
}

void TableView::layout() {
    const RectF v = body();
    m_scrollBar->setBounds({v.right() - ScrollBar::kWidth, v.y, ScrollBar::kWidth, v.height});
    m_scrollBar->setRange(static_cast<float>(m_count) * kRow, v.height);
    m_offset = std::clamp(m_offset, 0.0f, m_scrollBar->maxOffset());
    m_scrollBar->setOffset(m_offset);
}

void TableView::setRowCount(int count) {
    m_count = std::max(count, 0);
    if (m_selected >= m_count) {
        m_selected = m_count > 0 ? m_count - 1 : -1;
    }
    m_hoverRow = -1;
    layout();
    invalidate();
}

void TableView::setSelected(int row, bool reveal) {
    row = m_count > 0 ? std::clamp(row, 0, m_count - 1) : -1;
    if (row != m_selected) {
        m_selected = row;
        if (onSelect && row >= 0) {
            onSelect(row);
        }
    }
    if (reveal && row >= 0) {
        this->reveal(row);
    }
    invalidate();
}

RectF TableView::cellRect(int row, int column) const {
    const auto xs = columnXs();
    const RectF v = body();
    const float y = std::round(v.y + static_cast<float>(row) * kRow - m_offset);
    return {xs[static_cast<std::size_t>(column)], y, xs[static_cast<std::size_t>(column) + 1] - xs[static_cast<std::size_t>(column)],
            kRow};
}

RectF TableView::focusRect() const {
    if (m_selected >= 0) {
        const RectF v = body();
        const float y = v.y + static_cast<float>(m_selected) * kRow - m_offset;
        if (y >= v.y - 1 && y + kRow <= v.bottom() + 1) {
            return {v.x, y, v.width, kRow};
        }
    }
    return bounds();
}

void TableView::setOffset(float offset) {
    m_offset = std::clamp(offset, 0.0f, m_scrollBar->maxOffset());
    m_scrollBar->setOffset(m_offset);
    invalidate();
}

void TableView::reveal(int row) {
    const RectF v = body();
    const float top = static_cast<float>(row) * kRow;
    if (top < m_offset) {
        setOffset(top);
    } else if (top + kRow > m_offset + v.height) {
        setOffset(top + kRow - v.height);
    }
}

bool TableView::onWheel(PointF /*p*/, float lines) {
    if (!m_scrollBar->needed()) {
        return false;
    }
    setOffset(m_offset - lines * kRow);
    return true;
}

int TableView::rowAt(PointF p) const {
    const RectF v = body();
    if (!v.contains(p)) {
        return -1;
    }
    const int row = static_cast<int>(std::floor((p.y - v.y + m_offset) / kRow));
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
    if (row != m_hoverRow || column != m_hoverColumn) {
        m_hoverRow = row;
        m_hoverColumn = column;
        invalidate();
    }
}

void TableView::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hoverRow = m_hoverColumn = -1;
    }
    invalidate();
}

void TableView::onPointerDown(PointF p) {
    m_downRow = rowAt(p);
    m_downPoint = p;
    m_downColumn = m_downRow >= 0 ? columnAt(p.x) : -1;
    if (m_downRow >= 0) {
        setSelected(m_downRow, /*reveal=*/false);
    }
}

void TableView::onClick() {
    if (m_downRow >= 0 && m_downRow == m_hoverRow && onCellClick) {
        onCellClick(m_downRow, m_downColumn, m_downPoint);
    }
}

void TableView::onDoubleClick() {
    if (m_downRow >= 0 && onActivate) {
        onActivate(m_downRow);
    }
}

bool TableView::onKeyDown(const KeyEvent& key) {
    if (onKey && onKey(key)) {
        return true;
    }
    if (m_count == 0) {
        return false;
    }
    const int page = std::max(static_cast<int>(body().height / kRow) - 1, 1);
    switch (key.virtualKey) {
    case VK_DOWN: setSelected(m_selected < 0 ? 0 : m_selected + 1); return true;
    case VK_UP: setSelected(m_selected < 0 ? 0 : m_selected - 1); return true;
    case VK_NEXT: setSelected(m_selected + page); return true;
    case VK_PRIOR: setSelected(m_selected - page); return true;
    case VK_HOME: setSelected(0); return true;
    case VK_END: setSelected(m_count - 1); return true;
    case VK_RETURN:
    case VK_SPACE:
        if (m_selected >= 0 && onActivate) {
            onActivate(m_selected);
            return true;
        }
        return false;
    default: return false;
    }
}

void TableView::paint(Canvas& canvas) {
    const RectF b = bounds();
    const auto xs = columnXs();
    // Header.
    for (std::size_t i = 0; i < m_columns.size(); ++i) {
        const auto& c = m_columns[i];
        const RectF cell{xs[i] + kCellPad, b.y, xs[i + 1] - xs[i] - 2 * kCellPad, kHeader};
        canvas.drawText(c.title, cell, TypeStyle::Caption, Color::TextTertiary, c.align);
    }
    canvas.hairlineH(b.x, b.y + kHeader - (1.0f / canvas.scale()), b.width, Color::LineSubtle);

    const RectF v = body();
    canvas.pushClip(v);
    const int first = std::max(static_cast<int>(m_offset / kRow), 0);
    const int last = std::min(static_cast<int>((m_offset + v.height) / kRow) + 1, m_count - 1);
    for (int row = first; row <= last; ++row) {
        const float y = std::round(v.y + static_cast<float>(row) * kRow - m_offset);
        const RectF line{v.x, y, v.width, kRow};
        const bool selected = row == m_selected;
        const bool hovered = row == m_hoverRow;
        if (selected) {
            canvas.fillRect(line, Color::AccentSubtle);
        } else if (hovered) {
            canvas.fillRect(line, Color::BgRaised);
        }
        canvas.hairlineH(line.x, line.bottom() - (1.0f / canvas.scale()), line.width, Color::LineSubtle);
        if (paintCell) {
            for (std::size_t i = 0; i < m_columns.size(); ++i) {
                const RectF cell{xs[i] + kCellPad, y, xs[i + 1] - xs[i] - 2 * kCellPad, kRow};
                paintCell(canvas, row, static_cast<int>(i), cell,
                          CellState{hovered, selected, hovered && static_cast<int>(i) == m_hoverColumn});
            }
        }
    }
    canvas.popClip();
}

} // namespace wl::ui
