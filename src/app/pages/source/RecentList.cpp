#include "app/pages/source/RecentList.h"

#include "app/Format.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kRow = 24.0f;
constexpr float kTypeWidth = 96.0f;
constexpr float kVersionWidth = 208.0f;
constexpr float kSizeWidth = 80.0f;
constexpr float kSizeGap = 8.0f;
constexpr float kLastWidth = 140.0f;
constexpr float kIconX = 2.0f;
constexpr float kTextX = 22.0f;

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

float RecentList::contentHeight() const noexcept {
    return kRow * static_cast<float>(m_entries.size() + 1);
}

RecentList::Columns RecentList::columns() const {
    const RectF b = bounds();
    Columns c{};
    c.right = b.right();
    c.last = c.right - kLastWidth;
    c.sizeRight = c.last - kSizeGap;
    c.version = c.sizeRight - kSizeWidth - kVersionWidth;
    c.type = c.version - kTypeWidth;
    c.name = b.x;
    return c;
}

RectF RecentList::rowRect(int index) const {
    const RectF b = bounds();
    return {b.x, b.y + kRow * static_cast<float>(index + 1), b.width, kRow};
}

int RecentList::rowAt(ui::PointF p) const {
    const RectF b = bounds();
    const int index = static_cast<int>(std::floor((p.y - b.y) / kRow)) - 1;
    return index >= 0 && index < static_cast<int>(m_entries.size()) && b.contains(p) ? index : -1;
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
    if (row != m_hoverRow) {
        m_hoverRow = row;
        // Missing files explain themselves on hover.
        const bool missing = row >= 0 && !m_exists[static_cast<std::size_t>(row)];
        setTooltip(missing ? m_strings.get(Str::SourceFileMissing) : std::wstring{});
        invalidate();
    }
}

void RecentList::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hoverRow = -1;
    }
    invalidate();
}

void RecentList::onPointerDown(ui::PointF p) {
    select(rowAt(p));
}

void RecentList::onDoubleClick() {
    openSelected();
}

bool RecentList::onKeyDown(const ui::KeyEvent& key) {
    const int count = static_cast<int>(m_entries.size());
    if (count == 0) {
        return false;
    }
    switch (key.virtualKey) {
    case VK_UP: select(std::max(m_selected - 1, 0)); return true;
    case VK_DOWN: select(std::min(m_selected + 1, count - 1)); return true;
    case VK_HOME: select(0); return true;
    case VK_END: select(count - 1); return true;
    case VK_RETURN: openSelected(); return true;
    default: return false;
    }
}

RectF RecentList::focusRect() const {
    // Keyboard focus ring goes around the selected row, not the whole table.
    return m_selected >= 0 ? rowRect(m_selected) : bounds();
}

void RecentList::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const Columns c = columns();
    // Column header (caption, text.tertiary)
    const RectF header{b.x, b.y, b.width, kRow};
    auto headerText = [&](Str key, float x, float w, ui::TextAlign align = ui::TextAlign::Leading) {
        canvas.drawText(m_strings.get(key), {x, header.y, w, kRow}, TypeStyle::Caption, Color::TextTertiary, align);
    };
    headerText(Str::CommonName, c.name, c.type - c.name);
    headerText(Str::CommonType, c.type, kTypeWidth);
    headerText(Str::SourceVersion, c.version, kVersionWidth);
    headerText(Str::CommonSize, c.sizeRight - kSizeWidth, kSizeWidth, ui::TextAlign::Trailing);
    headerText(Str::SourceLastOpened, c.last, kLastWidth);
    canvas.hairlineH(b.x, header.bottom() - 1.0f / canvas.scale(), b.width, Color::LineSubtle);

    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const auto& e = m_entries[static_cast<std::size_t>(i)];
        const RectF row = rowRect(i);
        if (i == m_selected && focused()) {
            canvas.fillRoundRect(row, ui::tokens::radius::r2, Color::AccentSubtle);
        } else if (i == m_hoverRow) {
            canvas.fillRoundRect(row, ui::tokens::radius::r2, Color::BgRaised);
        }
        canvas.hairlineH(row.x, row.bottom() - 1.0f / canvas.scale(), row.width, Color::LineSubtle);
        const bool exists = m_exists[static_cast<std::size_t>(i)];
        const Color ink = exists ? Color::TextPrimary : Color::TextDisabled;
        canvas.drawIcon(iconFor(e.format), {row.x + kIconX, row.y + 4}, exists ? Color::TextSecondary : Color::TextDisabled);
        canvas.drawText(e.path.filename().wstring().empty() ? e.path.wstring() : e.path.filename().wstring(),
                        {row.x + kTextX, row.y, c.type - row.x - kTextX - 8, kRow}, TypeStyle::Body, ink);
        canvas.drawText(e.format == L"Folder" ? L"—" : e.format, {c.type, row.y, kTypeWidth - 8, kRow}, TypeStyle::Body, ink);
        canvas.drawText(e.summary, {c.version, row.y, kVersionWidth - 8, kRow}, TypeStyle::Body, ink);
        canvas.drawText(formatBytes(e.size, m_language), {c.sizeRight - kSizeWidth, row.y, kSizeWidth, kRow}, TypeStyle::Mono,
                        ink, ui::TextAlign::Trailing);
        canvas.drawText(formatRecentTime(e.lastOpened, m_language, m_strings), {c.last, row.y, kLastWidth, kRow},
                        TypeStyle::Mono, ink);
    }
}

} // namespace wl::app
