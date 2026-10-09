#include "app/shell/CommandPalette.h"

#include "base/Text.h"

#include "ui/widget/Host.h"
#include "ui/widgets/Kbd.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kWidth = 560.0f;
constexpr float kTop = 120.0f;
constexpr float kMargin = 8.0f;       // to the window edges when it is small
constexpr float kSearchRow = 40.0f;
constexpr float kGroupPad = 8.0f;     // above the group title and under the last row
constexpr float kHeader = 24.0f;
constexpr float kResultRow = 32.0f;
constexpr float kCommandRow = 24.0f;
constexpr float kSide = 16.0f;
constexpr float kTextX = 40.0f;       // icon 16 + gap 8 after the side padding
constexpr float kRowInset = 4.0f;     // the selected row's background
constexpr float kNameLine = 16.0f;
constexpr float kDetailLine = 14.0f;
constexpr float kEmpty = 72.0f;
constexpr float kKeysGap = 8.0f;
} // namespace

CommandPalette::CommandPalette(Labels labels, Search search) : m_labels(std::move(labels)), m_search(std::move(search)) {
    m_input = &add<ui::SearchBox>(m_labels.placeholder, std::vector<std::wstring>{m_labels.escKey});
    m_input->setBare(true);
    m_input->onChange = [this](const std::wstring& text) {
        m_query = text;
        refresh();
    };
    m_input->onSubmit = [this] { run(m_selected); };
    setAccessible(ui::AccessRole::List, m_labels.placeholder);
    refresh();
}

void CommandPalette::setQuery(std::wstring query) {
    m_input->setText(query);
    m_query = std::move(query);
    refresh();
}

void CommandPalette::refresh() {
    m_found = m_search ? m_search(m_query) : PaletteResults{};
    m_selected = 0;
    updateCompletion();
    layout();
    invalidate();
}

int CommandPalette::count() const noexcept {
    return static_cast<int>(m_found.results.size() + m_found.commands.size());
}

const PaletteItem& CommandPalette::itemAt(int index) const {
    const auto i = static_cast<std::size_t>(index);
    return i < m_found.results.size() ? m_found.results[i] : m_found.commands[i - m_found.results.size()];
}

void CommandPalette::updateCompletion() {
    // The rest of the selected name, when the text typed so far is how it begins.
    std::wstring rest;
    if (!m_query.empty() && m_selected >= 0 && m_selected < count()) {
        const std::wstring& name = itemAt(m_selected).name;
        if (name.size() > m_query.size() &&
            wl::text::fold(name).starts_with(wl::text::fold(m_query))) {
            rest = name.substr(m_query.size());
        }
    }
    m_input->setCompletion(std::move(rest));
}

void CommandPalette::select(int index) {
    if (index == m_selected || index < 0 || index >= count()) {
        return;
    }
    m_selected = index;
    updateCompletion();
    invalidate();
}

void CommandPalette::run(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    // Copies: closing pops the modal, which destroys this palette.
    const PaletteItem item = itemAt(index);
    const auto close = onClose;
    const auto handler = onRun;
    if (close) {
        close();
    }
    if (handler) {
        handler(item);
    }
}

void CommandPalette::layout() {
    const RectF b = bounds();
    const auto results = static_cast<float>(m_found.results.size());
    const auto commands = static_cast<float>(m_found.commands.size());
    float height = kSearchRow;
    if (results > 0) {
        height += kGroupPad + kHeader + results * kResultRow + kGroupPad;
    }
    if (commands > 0) {
        height += kGroupPad + kHeader + commands * kCommandRow + kGroupPad;
    }
    if (count() == 0) {
        height += kEmpty;
    }
    const float width = std::min(kWidth, std::max(b.width - 2 * kMargin, 0.0f));
    // 120 from the top; a short window pushes it up rather than off the bottom.
    const float top = std::max(std::min(kTop, b.height - kMargin - height), kMargin);
    m_panel = {b.x + std::round((b.width - width) / 2), b.y + top, width, height};
    m_input->setBounds({m_panel.x, m_panel.y, m_panel.width, kSearchRow});
}

RectF CommandPalette::rowRect(int index) const {
    const int results = static_cast<int>(m_found.results.size());
    float y = m_panel.y + kSearchRow;
    if (results > 0) {
        y += kGroupPad + kHeader;
        if (index < results) {
            return {m_panel.x, y + static_cast<float>(index) * kResultRow, m_panel.width, kResultRow};
        }
        y += static_cast<float>(results) * kResultRow + kGroupPad;
    }
    y += kGroupPad + kHeader;
    return {m_panel.x, y + static_cast<float>(index - results) * kCommandRow, m_panel.width, kCommandRow};
}

int CommandPalette::rowAt(ui::PointF p) const {
    for (int i = 0; i < count(); ++i) {
        if (rowRect(i).contains(p)) {
            return i;
        }
    }
    return -1;
}

void CommandPalette::paintRow(ui::Canvas& canvas, const PaletteItem& item, RectF row, bool selected) const {
    const bool result = item.kind != PaletteItem::Kind::Command;
    if (selected) {
        canvas.fillRoundRect({row.x + kRowInset, row.y, row.width - 2 * kRowInset, row.height}, ui::tokens::radius::r2,
                             Color::BgRaised);
    }
    canvas.drawIcon(item.icon, {row.x + kSide, row.y + (result ? 8.0f : 4.0f)}, Color::TextSecondary);

    // The keycap: ↵ on the selected result, the shortcut on a command.
    float right = row.right() - kSide;
    if (result && selected) {
        right = ui::Kbd::paintKeys(canvas, {m_labels.enterKey}, right, row.y + row.height / 2) - kKeysGap;
    } else if (!result && !item.keys.empty()) {
        right = ui::Kbd::paintKeys(canvas, item.keys, right, row.y + row.height / 2) - kKeysGap;
    }

    const float x = row.x + kTextX;
    const float width = std::max(right - x, 0.0f);
    const TypeStyle style = selected ? TypeStyle::BodyStrong : TypeStyle::Body;
    const RectF name = result ? RectF{x, row.y + 3, width, kNameLine}
                              : RectF{x, row.y + (row.height - kNameLine) / 2, width, kNameLine};
    if (item.markLength > 0 && item.markAt + item.markLength <= item.name.size()) {
        const std::wstring_view text = item.name;
        const float x0 = x + canvas.text().measure(text.substr(0, item.markAt), style);
        const float x1 = x + canvas.text().measure(text.substr(0, item.markAt + item.markLength), style);
        if (x0 < name.right()) {
            canvas.fillRect({x0, name.y, std::min(x1, name.right()) - x0, name.height}, Color::AccentSubtle);
        }
    }
    canvas.drawText(item.name, name, style, Color::TextPrimary);
    if (result) {
        canvas.drawText(item.detail, {x, row.y + 17, width, kDetailLine}, TypeStyle::Caption, Color::TextTertiary);
    }
}

void CommandPalette::paint(ui::Canvas& canvas) {
    const float radius = ui::tokens::radius::r3;
    canvas.dropShadow(m_panel, radius, ui::tokens::elevation::dialog);
    canvas.fillRoundRect(m_panel, radius, Color::BgOverlay);
    canvas.strokeRoundRect(m_panel, radius, Color::LineStrong);
    // Inside the 1px border.
    const float lineX = m_panel.x + 1;
    const float lineW = m_panel.width - 2;
    canvas.hairlineH(lineX, m_panel.y + kSearchRow - 1, lineW, Color::LineSubtle);

    if (count() == 0) {
        const float y = m_panel.y + kSearchRow + (kEmpty - kNameLine - kDetailLine - 2) / 2;
        canvas.drawText(m_labels.noResults, {m_panel.x + kSide, y, m_panel.width - 2 * kSide, kNameLine}, TypeStyle::Body,
                        Color::TextSecondary, ui::TextAlign::Center);
        canvas.drawText(m_labels.noResultsHint,
                        {m_panel.x + kSide, y + kNameLine + 2, m_panel.width - 2 * kSide, kDetailLine}, TypeStyle::Caption,
                        Color::TextTertiary, ui::TextAlign::Center);
        return;
    }

    int index = 0;
    float y = m_panel.y + kSearchRow;
    bool first = true;
    auto group = [&](const std::wstring& title, const std::vector<PaletteItem>& items, float rowHeight) {
        if (items.empty()) {
            return;
        }
        if (!first) {
            canvas.hairlineH(lineX, y, lineW, Color::LineSubtle);
        }
        first = false;
        canvas.drawText(title, {m_panel.x + kSide, y + kGroupPad, m_panel.width - 2 * kSide, kHeader}, TypeStyle::Section,
                        Color::TextSecondary);
        y += kGroupPad + kHeader;
        for (const auto& item : items) {
            paintRow(canvas, item, {m_panel.x, y, m_panel.width, rowHeight}, index == m_selected);
            y += rowHeight;
            ++index;
        }
        y += kGroupPad;
    };
    group(m_labels.results + L" · " + std::to_wstring(m_found.total), m_found.results, kResultRow);
    group(m_labels.commands, m_found.commands, kCommandRow);
}

bool CommandPalette::onKeyDown(const ui::KeyEvent& key) {
    // Keys the input did not take (it keeps ← → Home End and the editing keys).
    const int n = count();
    switch (key.virtualKey) {
    case VK_ESCAPE:
        if (const auto close = onClose) {
            close();
        }
        return true;
    case VK_DOWN:
        if (n > 0) {
            select((m_selected + 1) % n);
        }
        return true;
    case VK_UP:
        if (n > 0) {
            select((m_selected + n - 1) % n);
        }
        return true;
    case VK_PRIOR: select(0); return true;
    case VK_NEXT: select(n - 1); return true;
    case 'K':
        if (key.ctrl && !key.shift && !key.alt) { // the shortcut that opened it closes it
            if (const auto close = onClose) {
                close();
            }
            return true;
        }
        break;
    default: break;
    }
    return true; // modal: nothing falls through to the shell's shortcuts
}

void CommandPalette::onPointerMove(ui::PointF p) {
    if (const int row = rowAt(p); row >= 0) {
        select(row);
    }
}

void CommandPalette::onPointerUp(ui::PointF p) {
    m_lastUp = p;
}

void CommandPalette::onClick() {
    // Clicks reach the palette itself on a row, on the panel's padding, or on the scrim.
    if (!m_panel.contains(m_lastUp)) {
        if (const auto close = onClose) {
            close();
        }
        return;
    }
    if (const int row = rowAt(m_lastUp); row >= 0) {
        run(row);
    }
}

bool CommandPalette::onWheel(ui::PointF /*p*/, float /*lines*/) {
    return true;
}

} // namespace wl::app
