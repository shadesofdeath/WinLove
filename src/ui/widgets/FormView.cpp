#include "ui/widgets/FormView.h"

#include <algorithm>

namespace wl::ui {

namespace {
using tokens::Color;
using tokens::TypeStyle;
constexpr float kHintGap = 8.0f;
constexpr float kRowPad = (FormView::kRow - tokens::size::control) / 2;
} // namespace

FormView::FormView(float labelWidth) : m_labelWidth(labelWidth) {
    clear();
}

void FormView::clear() {
    clearChildren();
    m_rows.clear();
    m_offset = 0;
    m_pinned = -1;
    m_scroll = &add<ScrollBar>();
    m_scroll->onScroll = [this](float offset) { setOffset(offset, /*pinned=*/false); };
    layout();
    invalidate();
}

void FormView::addSection(std::wstring title) {
    m_rows.push_back(Row{std::move(title), {}, nullptr, 0});
    invalidate();
}

void FormView::appendRow(std::wstring label, std::wstring hint, float width, Widget* control) {
    m_rows.push_back(Row{std::move(label), std::move(hint), control, width});
    bringToFront(m_scroll); // the bar stays above the controls
    layout();
    invalidate();
}

void FormView::setHint(const Widget& control, std::wstring hint, tokens::Color color) {
    const auto row = std::ranges::find(m_rows, &control, &Row::control);
    if (row != m_rows.end() && (row->hint != hint || row->hintColor != color)) {
        row->hint = std::move(hint);
        row->hintColor = color;
        invalidate();
    }
}

float FormView::contentHeight() const {
    float height = 0;
    for (const auto& row : m_rows) {
        height += row.control ? kRow : kSection;
    }
    return height;
}

int FormView::sectionCount() const noexcept {
    return static_cast<int>(std::ranges::count_if(m_rows, [](const Row& row) { return row.control == nullptr; }));
}

float FormView::sectionTop(int index) const {
    float y = 0;
    int section = 0;
    for (const auto& row : m_rows) {
        if (!row.control && section++ == index) {
            return y;
        }
        y += row.control ? kRow : kSection;
    }
    return -1;
}

int FormView::currentSection() const {
    if (m_pinned >= 0) {
        return m_pinned;
    }
    int current = 0;
    for (int i = 0; i < sectionCount(); ++i) {
        if (sectionTop(i) <= m_offset + kSection / 2) {
            current = i;
        }
    }
    return current;
}

void FormView::scrollToSection(int index) {
    const float top = sectionTop(index);
    if (top >= 0) {
        m_pinned = index; // before the scroll: onScroll listeners ask currentSection()
        setOffset(top, /*pinned=*/true);
    }
}

void FormView::setOffset(float offset, bool pinned) {
    m_offset = offset;
    if (!pinned) {
        m_pinned = -1;
    }
    layout();
    invalidate();
    if (onScroll) {
        onScroll();
    }
}

void FormView::layout() {
    const RectF b = bounds();
    const float content = contentHeight();
    m_offset = std::clamp(m_offset, 0.0f, std::max(content - b.height, 0.0f));
    float y = b.y - m_offset;
    const float x = b.x + m_labelWidth;
    for (const auto& row : m_rows) {
        if (!row.control) {
            y += kSection;
            continue;
        }
        const float width = row.width > 0 ? row.width : row.control->measure({}).width;
        row.control->setBounds({x, y + kRowPad, width, tokens::size::control});
        y += kRow;
    }
    if (m_scroll) {
        m_scroll->setBounds({b.right() - ScrollBar::kWidth, b.y, ScrollBar::kWidth, b.height});
        m_scroll->setRange(content, b.height);
        m_scroll->setOffset(m_offset);
        m_scroll->setVisible(m_scroll->needed());
    }
}

void FormView::paint(Canvas& canvas) {
    const RectF b = bounds();
    canvas.pushClip(b);
    float y = b.y - m_offset;
    for (const auto& row : m_rows) {
        if (!row.control) {
            canvas.drawText(row.label, {b.x, y + 10, b.width, 20}, TypeStyle::Section, Color::TextSecondary);
            canvas.hairlineH(b.x, y + kSection - 1, b.width, Color::LineSubtle);
            y += kSection;
            continue;
        }
        canvas.drawText(row.label, {b.x, y, m_labelWidth - 8, kRow}, TypeStyle::Body, Color::TextSecondary);
        if (!row.hint.empty()) {
            const float hx = row.control->bounds().right() + kHintGap;
            canvas.drawText(row.hint, {hx, y, std::max(b.right() - hx, 0.0f), kRow}, TypeStyle::Caption, row.hintColor);
        }
        y += kRow;
    }
    canvas.popClip();
}

bool FormView::onWheel(PointF /*p*/, float lines) {
    if (!m_scroll || !m_scroll->needed()) {
        return false;
    }
    setOffset(m_offset - lines * kRow, /*pinned=*/false);
    return true;
}

void FormView::reveal(const Widget& control) {
    const RectF b = bounds();
    const RectF r = control.bounds();
    if (r.y - kRowPad < b.y) {
        setOffset(m_offset - (b.y - (r.y - kRowPad)), /*pinned=*/false);
    } else if (r.bottom() + kRowPad > b.bottom()) {
        setOffset(m_offset + r.bottom() + kRowPad - b.bottom(), /*pinned=*/false);
    }
}

} // namespace wl::ui
