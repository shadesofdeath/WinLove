#include "ui/widgets/FormView.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
using tokens::TypeStyle;
constexpr float kHintGap = 8.0f;
constexpr float kRowPad = (FormView::kRow - FormView::kControl) / 2;
constexpr float kLabelInset = 16.0f; // D-114: inside the section card
constexpr float kMark = 6.0f; // D-107: the changed-row dot after the label
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

void FormView::setMarked(const Widget& control, bool marked) {
    const auto row = std::ranges::find(m_rows, &control, &Row::control);
    if (row != m_rows.end() && row->marked != marked) {
        row->marked = marked;
        invalidate();
    }
}

void FormView::setRowVisible(const Widget& control, bool visible) {
    const auto row = std::ranges::find(m_rows, &control, &Row::control);
    if (row != m_rows.end() && row->hidden == visible) {
        row->hidden = !visible;
        updateSections();
        layout();
        invalidate();
    }
}

void FormView::updateSections() {
    // A section shows while one of its rows does.
    Row* section = nullptr;
    bool any = false;
    for (auto& row : m_rows) {
        if (!row.control) {
            if (section) {
                section->hidden = !any;
            }
            section = &row;
            any = false;
        } else {
            any = any || !row.hidden;
        }
    }
    if (section) {
        section->hidden = !any;
    }
}

namespace {
float heightOf(bool control, bool hidden) {
    return hidden ? 0.0f : control ? FormView::kRow : FormView::kSection;
}
} // namespace

float FormView::contentHeight() const {
    float height = sectionCount() > 0 ? 12.0f : 0.0f; // D-114: room under the last card
    for (const auto& row : m_rows) {
        height += heightOf(row.control != nullptr, row.hidden);
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
        y += heightOf(row.control != nullptr, row.hidden);
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
            y += heightOf(false, row.hidden);
            continue;
        }
        row.control->setVisible(!row.hidden);
        if (row.hidden) {
            continue;
        }
        const float width = row.width > 0 ? row.width : row.control->measure({}).width;
        row.control->setBounds({x, y + kRowPad, width, kControl});
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
        if (row.hidden) {
            continue;
        }
        if (!row.control) {
            canvas.drawText(row.label, {b.x + 4, y + kSection - 26, b.width, 20}, TypeStyle::BodySemi, Color::TextPrimary);
            y += kSection;
            continue;
        }
        // D-114: the card of this run of rows (drawn at its first row) and a hairline above every other.
        const float cardWidth = b.width - (m_scroll && m_scroll->needed() ? ScrollBar::kWidth + 4 : 0.0f);
        const std::size_t at = static_cast<std::size_t>(&row - m_rows.data());
        std::size_t previous = at;
        while (previous > 0 && m_rows[previous - 1].hidden) {
            --previous;
        }
        const bool first = previous == 0 || !m_rows[previous - 1].control; // the first visible row of its run
        if (first) {
            float height = 0;
            for (std::size_t i = at; i < m_rows.size() && m_rows[i].control; ++i) {
                height += m_rows[i].hidden ? 0.0f : kRow;
            }
            canvas.fillRoundRect({b.x, y, cardWidth, height}, tokens::radius::r3, Color::BgRaised);
        } else {
            canvas.hairlineH(b.x + kLabelInset, y, cardWidth - 2 * kLabelInset, Color::LineSubtle);
        }
        const float labelRoom = m_labelWidth - 12 - kLabelInset - (row.marked ? kMark + 8 : 0.0f);
        const float labelWidth = std::min(std::ceil(canvas.text().measure(row.label, TypeStyle::Body)) + 1, labelRoom);
        canvas.drawText(row.label, {b.x + kLabelInset, y, labelWidth, kRow}, TypeStyle::Body, Color::TextPrimary);
        if (row.marked) {
            // The reference tool's "Değiştirilen satırlar: Kenar çubuğu" — on the card's left edge.
            canvas.fillRoundRect({b.x + 4, y + 7, 3.0f, kRow - 14}, 1.5f, Color::AccentBase);
            canvas.fillEllipse({b.x + kLabelInset + labelWidth + 8 + kMark / 2, y + kRow / 2}, kMark / 2, Color::AccentBase);
        }
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
