#include "ui/widgets/FormView.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
using tokens::TypeStyle;
constexpr float kHintGap = tokens::spacing::s4;
constexpr float kRowPad = (FormView::kRow - FormView::kControl) / 2;
constexpr float kLabelInset = tokens::spacing::s6; // D-114: inside the section card
constexpr float kTitleInset = tokens::spacing::s2; // the section title, a little in from the card's edge
constexpr float kLabelGap = tokens::spacing::s5;   // a label ends this far before its control
constexpr float kBottomRoom = tokens::spacing::s5; // D-114: room under the last card
// The changed row's bar: on the card's left edge, a spacing step in from the row's top and bottom.
constexpr float kMarkX = tokens::spacing::s2;
constexpr float kMarkWidth = 3.0f;
constexpr float kMarkInsetY = tokens::spacing::s4;
} // namespace

void FormView::paintSectionTitle(Canvas& canvas, RectF area, std::wstring_view title) {
    canvas.drawText(title, {area.x, area.bottom() - kSectionBelow - kSectionLine, std::max(area.width, 0.0f), kSectionLine},
                    TypeStyle::Section, Color::TextPrimary);
}

FormView::FormView(float labelWidth) : m_labelWidth(labelWidth) {
    clear();
}

void FormView::clear() {
    clearChildren();
    m_rows.clear();
    m_offset = 0;
    m_pinned = -1;
    m_tipRow = -1;
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
    float height = sectionCount() > 0 ? kBottomRoom : 0.0f; // D-114: room under the last card
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
    // Scrolling moves the rows, nothing else: each control keeps the width it was last measured
    // to. Measuring every row again (a dropdown measures all its items) for each wheel step or
    // dragged pixel was most of the cost of scrolling a long form (audit UI-02).
    m_scrolling = true;
    layout();
    m_scrolling = false;
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
        if (m_scrolling) {
            if (!row.hidden) {
                const RectF placed = row.control->bounds(); // laid out when the row was added / shown
                row.control->setBounds({x, y + kRowPad, placed.width, kControl});
                y += kRow;
            }
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
        if (y >= b.bottom()) {
            break; // everything from here on is below the view (audit UI-02)
        }
        if (!row.control) {
            if (y + kSection > b.y) {
                paintSectionTitle(canvas, {b.x + kTitleInset, y, b.width - kTitleInset, kSection}, row.label);
            }
            y += kSection;
            continue;
        }
        // D-114: the card of this run of rows (drawn at its first row) and a hairline above every other.
        const float card = cardWidth();
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
            canvas.fillRoundRect({b.x, y, card, height}, tokens::radius::r3, Color::BgRaised);
        }
        if (y + kRow <= b.y) {
            y += kRow; // scrolled above the view: its card (drawn above, it may reach in) is all that shows
            continue;
        }
        if (!first) {
            canvas.hairlineH(b.x + kLabelInset, y, card - 2 * kLabelInset, Color::LineSubtle);
        }
        const float labelWidth = std::min(std::ceil(canvas.text().measure(row.label, TypeStyle::Body)) + 1, labelRoom());
        canvas.drawText(row.label, {b.x + kLabelInset, y, labelWidth, kRow}, TypeStyle::Body, Color::TextPrimary);
        if (row.marked) {
            // The reference tool's "Değiştirilen satırlar: Kenar çubuğu" — on the card's left edge. With the
            // row's hint ("değiştirilecek") that is the whole signal: the dot after the label is gone
            // (2026-11-10 polish: three marks for one change were two too many).
            canvas.fillRoundRect({b.x + kMarkX, y + kMarkInsetY, kMarkWidth, kRow - 2 * kMarkInsetY}, kMarkWidth / 2,
                                 Color::AccentBase);
        }
        if (!row.hint.empty()) {
            // One line, ending in "…" (the text styles trim) where the card's inner padding starts:
            // a long hint used to run on to the card's edge, or under the scroll bar.
            if (const RectF hint = hintBox(*row.control, y); hint.width > 0) {
                canvas.drawText(row.hint, hint, TypeStyle::Caption, row.hintColor);
            }
        }
        y += kRow;
    }
    canvas.popClip();
}

float FormView::cardWidth() const {
    return bounds().width - (m_scroll && m_scroll->needed() ? ScrollBar::kWidth + 4 : 0.0f);
}

RectF FormView::hintBox(const Widget& control, float rowY) const {
    const float x = control.bounds().right() + kHintGap;
    const float right = bounds().x + cardWidth() - kLabelInset; // the label's inset, mirrored
    return {x, rowY, std::max(right - x, 0.0f), kRow};
}

RectF FormView::hintRect(const Widget& control) const {
    return hintBox(control, control.bounds().y - kRowPad);
}

bool FormView::onWheel(PointF p, float lines) {
    if (!m_scroll || !m_scroll->needed()) {
        return false;
    }
    setOffset(m_offset - lines * kRow, /*pinned=*/false);
    onPointerMove(p); // the label under the pointer changed
    return true;
}

float FormView::labelRoom() const noexcept {
    return m_labelWidth - kLabelGap - kLabelInset;
}

void FormView::onPointerMove(PointF p) {
    const RectF b = bounds();
    int found = -1;
    RectF label{};
    float y = b.y - m_offset;
    for (std::size_t i = 0; i < m_rows.size() && found < 0; ++i) {
        const Row& row = m_rows[i];
        const float h = heightOf(row.control != nullptr, row.hidden);
        if (row.control && h > 0 && p.y >= y && p.y < y + h && p.x >= b.x + kLabelInset && p.x < b.x + kLabelInset + labelRoom() &&
            textWidth(row.label, TypeStyle::Body) + 1 > labelRoom()) { // measured on a move, not while painting
            found = static_cast<int>(i);
            label = {b.x + kLabelInset, y, labelRoom(), h};
        }
        y += h;
    }
    if (found != m_tipRow) {
        m_tipRow = found;
        m_tipRect = label;
        setTooltip(found >= 0 ? m_rows[static_cast<std::size_t>(found)].label : std::wstring{});
    }
}

void FormView::onHoverChanged(bool hovered) {
    if (!hovered && m_tipRow >= 0) {
        m_tipRow = -1;
        setTooltip({});
    }
    invalidate();
}

RectF FormView::tooltipAnchor() const {
    return m_tipRow >= 0 ? m_tipRect : bounds();
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
