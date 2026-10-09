#include "app/pages/source/RecentList.h"

#include "app/Format.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Dropdown.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kCardHeight = 64.0f;
constexpr float kCardMinWidth = 280.0f;
constexpr float kGap = 8.0f;
constexpr float kPad = 12.0f;
constexpr float kTile = 36.0f;       // the icon's tile
constexpr float kRemoveInset = 8.0f; // the x button: 16px icon, this far from the card's corner

ui::icons::Icon iconFor(const std::wstring& format) {
    if (format == L"ISO") return ui::icons::Icon::DiscIso;
    if (format == L"Folder") return ui::icons::Icon::Folder;
    return ui::icons::Icon::ImageWim;
}
} // namespace

RecentList::RecentList(const Localization& strings, Language language) : m_strings(strings), m_language(language) {
    setFocusable(true);
    setAccessible(ui::AccessRole::List, strings.get(Str::SourceRecent));
}

void RecentList::setEntries(std::vector<RecentSource> entries) {
    m_entries = std::move(entries);
    m_exists.clear();
    for (const auto& e : m_entries) {
        std::error_code ec;
        m_exists.push_back(std::filesystem::exists(e.path, ec));
    }
    m_selected = m_entries.empty() ? -1 : std::min(std::max(m_selected, 0), static_cast<int>(m_entries.size()) - 1);
    m_hoverRow = -1;
    invalidate();
}

int RecentList::columnCount() const noexcept {
    const float width = bounds().width;
    return std::clamp(static_cast<int>((width + kGap) / (kCardMinWidth + kGap)), 1, 4);
}

float RecentList::contentHeight() const noexcept {
    const int columns = columnCount();
    const int rows = (static_cast<int>(m_entries.size()) + columns - 1) / columns;
    return rows == 0 ? 0.0f : static_cast<float>(rows) * (kCardHeight + kGap) - kGap;
}

RectF RecentList::rowRect(int index) const {
    const RectF b = bounds();
    const int columns = columnCount();
    const float width = (b.width - kGap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const int col = index % columns;
    const int row = index / columns;
    return {std::round(b.x + static_cast<float>(col) * (width + kGap)), b.y + static_cast<float>(row) * (kCardHeight + kGap),
            std::floor(width), kCardHeight};
}

int RecentList::rowAt(ui::PointF p) const {
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (rowRect(i).contains(p)) {
            return i;
        }
    }
    return -1;
}

RectF RecentList::removeRect(int index) const {
    const RectF card = rowRect(index);
    return {card.right() - kRemoveInset - ui::tokens::size::icon, card.y + kRemoveInset, ui::tokens::size::icon,
            ui::tokens::size::icon};
}

void RecentList::remove(int index) {
    if (index >= 0 && index < static_cast<int>(m_entries.size()) && onRemove) {
        // A copy: the handler changes the list, and with it m_entries.
        const std::filesystem::path path = m_entries[static_cast<std::size_t>(index)].path;
        const auto handler = onRemove;
        handler(path);
    }
}

void RecentList::select(int index) {
    if (index >= 0 && index < static_cast<int>(m_entries.size()) && index != m_selected) {
        m_selected = index;
        invalidate();
    }
}

void RecentList::openSelected() {
    if (m_selected >= 0 && m_selected < static_cast<int>(m_entries.size()) && onOpen) {
        onOpen(m_entries[static_cast<std::size_t>(m_selected)].path);
    }
}

void RecentList::onPointerMove(ui::PointF p) {
    const int row = rowAt(p);
    const bool overRemove = row >= 0 && removeRect(row).contains(p);
    if (row != m_hoverRow || overRemove != m_hoverRemove) {
        m_hoverRow = row;
        m_hoverRemove = overRemove;
        // The x says what it does; missing files explain themselves on hover.
        const bool missing = row >= 0 && !m_exists[static_cast<std::size_t>(row)];
        setTooltip(overRemove ? m_strings.get(Str::SourceRemove)
                   : missing  ? m_strings.get(Str::SourceFileMissing)
                              : std::wstring{});
        invalidate();
    }
}

void RecentList::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hoverRow = -1;
        m_hoverRemove = false;
    }
    invalidate();
}

void RecentList::onPointerDown(ui::PointF p) {
    const int row = rowAt(p);
    m_downRemove = row >= 0 && removeRect(row).contains(p) ? row : -1;
    select(row);
}

void RecentList::onClick() {
    if (const int row = std::exchange(m_downRemove, -1); row >= 0) {
        remove(row);
        return;
    }
    if (m_hoverRow >= 0 && m_hoverRow == m_selected) {
        openSelected(); // a card opens with one click
    }
}

void RecentList::onDoubleClick() {}

bool RecentList::onKeyDown(const ui::KeyEvent& key) {
    const int count = static_cast<int>(m_entries.size());
    if (count == 0) {
        return false;
    }
    const int columns = columnCount();
    switch (key.virtualKey) {
    case VK_LEFT: select(std::max(m_selected - 1, 0)); return true;
    case VK_RIGHT: select(std::min(m_selected + 1, count - 1)); return true;
    case VK_UP: select(std::max(m_selected - columns, 0)); return true;
    case VK_DOWN: select(std::min(m_selected + columns, count - 1)); return true;
    case VK_HOME: select(0); return true;
    case VK_END: select(count - 1); return true;
    case VK_RETURN: openSelected(); return true;
    case VK_DELETE: remove(m_selected); return true;
    default: return false;
    }
}

bool RecentList::onContextMenu(ui::PointF p) {
    const int row = rowAt(p);
    if (row < 0 || !host()) {
        return false;
    }
    select(row);
    const std::filesystem::path path = m_entries[static_cast<std::size_t>(row)].path;
    // Copies of the handlers: picking an item may change the list under this widget.
    const auto open = onOpen;
    const auto show = onShowInFolder;
    const auto removeEntry = onRemove;
    const auto hash = onVerifyHash;
    std::vector<std::wstring> labels{m_strings.get(Str::CommonOpen), m_strings.get(Str::SourceShowInFolder)};
    std::vector<std::function<void(const std::filesystem::path&)>> handlers{open, show};
    std::error_code ec;
    if (hash && std::filesystem::is_regular_file(path, ec)) {
        labels.push_back(m_strings.get(Str::SourceHashMenu));
        handlers.push_back(hash);
    }
    labels.push_back(m_strings.get(Str::SourceRemove));
    handlers.push_back(removeEntry);
    auto popup = std::make_unique<ui::MenuPopup>(
        RectF{p.x, p.y, 0, 0}, std::move(labels), -1,
        [path, handlers = std::move(handlers)](int index) {
            if (index >= 0 && index < static_cast<int>(handlers.size()) && handlers[static_cast<std::size_t>(index)]) {
                handlers[static_cast<std::size_t>(index)](path);
            }
        },
        [] {});
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false);
    return true;
}

RectF RecentList::focusRect() const {
    // Keyboard focus ring goes around the selected row, not the whole table.
    return m_selected >= 0 ? rowRect(m_selected) : bounds();
}

void RecentList::paint(ui::Canvas& canvas) {
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const auto& e = m_entries[static_cast<std::size_t>(i)];
        const RectF card = rowRect(i);
        const bool selected = i == m_selected && focused();
        const bool hover = i == m_hoverRow;
        canvas.fillRoundRect(card, ui::tokens::radius::r3,
                             selected ? Color::AccentSubtle : hover ? Color::BgRaised : Color::BgPanel);
        canvas.strokeRoundRect(card, ui::tokens::radius::r3, hover || selected ? Color::LineStrong : Color::LineSubtle);
        const bool exists = m_exists[static_cast<std::size_t>(i)];
        const Color ink = exists ? Color::TextPrimary : Color::TextDisabled;
        const Color soft = exists ? Color::TextSecondary : Color::TextDisabled;

        const RectF tile{card.x + kPad, card.y + (kCardHeight - kTile) / 2, kTile, kTile};
        canvas.fillRoundRect(tile, ui::tokens::radius::r3, hover || selected ? Color::BgPressed : Color::BgRaised);
        canvas.drawIcon(iconFor(e.format), {tile.x + (kTile - 20) / 2, tile.y + (kTile - 20) / 2},
                        exists ? Color::AccentBase : Color::TextDisabled, ui::IconVariant::Regular16, 20.0f);

        const float textX = tile.right() + kPad;
        const float textW = std::max(card.right() - textX - kPad - (hover || selected ? 20.0f : 0.0f), 0.0f);
        const std::wstring name = e.path.filename().wstring().empty() ? e.path.wstring() : e.path.filename().wstring();
        canvas.drawText(name, {textX, card.y + 10, textW, 18}, TypeStyle::BodyStrong, ink);
        canvas.drawText(e.summary, {textX, card.y + 27, textW, 16}, TypeStyle::Caption, soft);
        const std::wstring meta = (e.format == L"Folder" ? m_strings.get(Str::SourceFolderType) : e.format) + L" \u00b7 " +
                                  formatBytes(e.size, m_language) + L" \u00b7 " +
                                  formatRecentTime(e.lastOpened, m_language, m_strings);
        canvas.drawText(exists ? meta : m_strings.get(Str::SourceFileMissing), {textX, card.y + 43, textW, 14},
                        TypeStyle::Caption, exists ? Color::TextTertiary : Color::StatusWarning);
        if (hover || selected) {
            const RectF x = removeRect(i);
            canvas.drawIcon(ui::icons::Icon::Close, {x.x, x.y}, hover && m_hoverRemove ? Color::TextPrimary : Color::TextTertiary);
        }
    }
}

} // namespace wl::app
