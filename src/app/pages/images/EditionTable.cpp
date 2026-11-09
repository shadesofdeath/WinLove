#include "app/pages/images/EditionTable.h"

#include "app/Format.h"
#include "ui/widgets/Checkbox.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
// D-107: the package's edition table (LAYOUT.md "INSTALL.WIM · SÜRÜMLER"): a card (panel color,
// radius 12, line border); header 34 in caption text.tertiary; rows 52 with a line on top, 14 at the
// sides and 12 between the columns. Dizin 44 (mono), Ad (name + edition id under it), Mimari 60,
// Derleme 130 (mono), Dil, Boyut 96 (right), Durum 130 ("● Bağlı"). A check box before the index
// marks more than one edition. The marked rows: accent.subtle with a 2px accent bar.
constexpr float kHead = ui::tokens::size::tableHead;
constexpr float kRow = ui::tokens::size::tableRow;
constexpr float kPadX = 12.0f;
constexpr float kColGap = 10.0f;
constexpr float kCheck = ui::tokens::size::checkbox;
constexpr float kIndex = 36.0f;
constexpr float kModified = 104.0f;
constexpr float kMinName = 220.0f; // optional columns drop out below this name width
constexpr float kArch = 52.0f;
constexpr float kBuild = 92.0f;
constexpr float kLang = 52.0f;
constexpr float kSize = 76.0f;
constexpr float kStatus = 76.0f;

// Fixed columns from the right; "Değiştirilme" only while the name keeps at least kMinName.
struct Columns {
    float check, index, name, nameEnd, arch, build, lang, modified, size, status;
    bool showModified;
    bool showArch = true; // D-114: on a narrow table the edition name keeps its room;
    bool showLang = true; // the inspector shows both anyway
};

Columns columnsFor(RectF b) {
    Columns c{};
    c.check = b.x + kPadX;
    c.index = c.check + kCheck + kColGap;
    c.name = c.index + kIndex + kColGap;
    const float right = b.right() - kPadX;
    const float fixed = kArch + kBuild + kLang + kSize + kStatus + 5 * kColGap;
    c.showModified = right - c.name - fixed - kModified - kColGap >= kMinName;
    c.status = right - kStatus;
    c.size = c.status - kColGap - kSize;
    c.modified = c.size - (c.showModified ? kColGap + kModified : 0.0f);
    const float nameAll = c.modified - c.name - (kLang + kBuild + kArch + 3 * kColGap) - kColGap;
    c.showArch = nameAll >= 170;
    c.showLang = nameAll + (c.showArch ? 0.0f : kArch + kColGap) >= 170;
    c.lang = c.showLang ? c.modified - kColGap - kLang : c.modified;
    c.build = c.lang - kColGap - kBuild;
    c.arch = c.showArch ? c.build - kColGap - kArch : c.build;
    c.nameEnd = c.arch - kColGap;
    return c;
}
} // namespace

EditionTable::EditionTable(const Localization& strings, Language language) : m_strings(strings), m_language(language) {
    m_scrollBar = &add<ui::ScrollBar>();
    m_scrollBar->onScroll = [this](float offset) { setOffset(offset); };
    setFocusable(true);
    setAccessible(ui::AccessRole::List, strings.get(Str::ImagesTitle));
}

void EditionTable::setImages(std::vector<core::ImageInfo> images) {
    m_images = std::move(images);
    m_hoverRow = -1;
    invalidate();
}

void EditionTable::setSelection(std::optional<int> primary, const std::vector<int>& marked) {
    EditionSelection next;
    for (const int index : marked) {
        if (const int row = rowOfIndex(index); row >= 0) {
            next.rows.push_back(row);
        }
    }
    std::ranges::sort(next.rows);
    next.primary = rowOfIndex(primary);
    // The Shift anchor survives a refresh as long as its row is still marked.
    next.anchor = next.contains(m_selection.anchor) ? m_selection.anchor : next.primary;
    m_selection = std::move(next);
    invalidate();
}

void EditionTable::setRowState(std::optional<int> index, RowState state, bool dimOthers) {
    m_stateIndex = index;
    m_state = state;
    m_dimOthers = dimOthers;
    invalidate();
}

float EditionTable::contentHeight() const noexcept {
    return kHead + kRow * static_cast<float>(m_images.size()) + 1;
}

RectF EditionTable::body() const {
    const RectF b = bounds();
    return {b.x, b.y + kHead, b.width, std::max(b.height - kHead - 1, 0.0f)};
}

RectF EditionTable::rowRect(int row) const {
    const RectF v = body();
    return {v.x, v.y + kRow * static_cast<float>(row) - m_offset, v.width, kRow};
}

int EditionTable::rowAt(ui::PointF p) const {
    const RectF v = body();
    if (!v.contains(p) || p.x >= v.right() - ui::ScrollBar::kWidth * (m_scrollBar->needed() ? 1.0f : 0.0f)) {
        return -1;
    }
    const int row = static_cast<int>(std::floor((p.y - v.y + m_offset) / kRow));
    return row >= 0 && row < static_cast<int>(m_images.size()) ? row : -1;
}

void EditionTable::layout() {
    const RectF v = body();
    m_scrollBar->setBounds({v.right() - ui::ScrollBar::kWidth - 2, v.y, ui::ScrollBar::kWidth, v.height});
    m_scrollBar->setRange(kRow * static_cast<float>(m_images.size()), v.height);
    m_offset = std::clamp(m_offset, 0.0f, m_scrollBar->maxOffset());
    m_scrollBar->setOffset(m_offset);
    m_scrollBar->setVisible(m_scrollBar->needed());
}

void EditionTable::setOffset(float offset) {
    m_offset = std::clamp(offset, 0.0f, m_scrollBar->maxOffset());
    m_scrollBar->setOffset(m_offset);
    invalidate();
}

void EditionTable::reveal(int row) {
    if (row < 0) {
        return;
    }
    const RectF v = body();
    const float top = kRow * static_cast<float>(row);
    if (top < m_offset) {
        setOffset(top);
    } else if (top + kRow > m_offset + v.height) {
        setOffset(top + kRow - v.height);
    }
}

bool EditionTable::onWheel(ui::PointF /*p*/, float lines) {
    if (!m_scrollBar->needed()) {
        return false;
    }
    setOffset(m_offset - lines * kRow);
    return true;
}

int EditionTable::rowOfIndex(std::optional<int> index) const {
    if (!index) {
        return -1;
    }
    for (int i = 0; i < static_cast<int>(m_images.size()); ++i) {
        if (m_images[static_cast<std::size_t>(i)].index == *index) {
            return i;
        }
    }
    return -1;
}

void EditionTable::publish() {
    invalidate();
    if (!onSelect || m_selection.primary < 0 || m_selection.primary >= static_cast<int>(m_images.size())) {
        return;
    }
    std::vector<int> marked;
    for (const int row : m_selection.rows) {
        if (row >= 0 && row < static_cast<int>(m_images.size())) {
            marked.push_back(m_images[static_cast<std::size_t>(row)].index);
        }
    }
    onSelect(m_images[static_cast<std::size_t>(m_selection.primary)].index, std::move(marked));
}

void EditionTable::onPointerMove(ui::PointF p) {
    const int row = rowAt(p);
    if (row != m_hoverRow) {
        m_hoverRow = row;
        invalidate();
    }
}

void EditionTable::onHoverChanged(bool hovered) {
    if (!hovered) {
        m_hoverRow = -1;
    }
    invalidate();
}

void EditionTable::onPointerDown(ui::PointF p) {
    const int row = rowAt(p);
    if (row < 0) {
        return;
    }
    // Pointer events carry no modifiers: the keyboard state at the click is what counts.
    if (GetKeyState(VK_SHIFT) < 0) {
        m_selection.extendTo(row);
    } else if (GetKeyState(VK_CONTROL) < 0 || p.x < bounds().x + kPadX + kCheck + kColGap / 2) {
        m_selection.toggle(row);
    } else {
        m_selection.only(row);
    }
    publish();
}

void EditionTable::onDoubleClick() {
    if (m_selection.primary >= 0 && m_selection.rows.size() == 1 && onActivate) {
        onActivate(m_images[static_cast<std::size_t>(m_selection.primary)].index);
    }
}

bool EditionTable::onKeyDown(const ui::KeyEvent& key) {
    const int count = static_cast<int>(m_images.size());
    if (count == 0) {
        return false;
    }
    const int current = std::clamp(m_selection.primary, 0, count - 1);
    auto moveTo = [&](int row) {
        if (key.shift) {
            m_selection.extendTo(row);
        } else {
            m_selection.only(row);
        }
        reveal(row);
        publish();
        return true;
    };
    switch (key.virtualKey) {
    case VK_UP: return moveTo(std::max(current - 1, 0));
    case VK_DOWN: return moveTo(std::min(current + 1, count - 1));
    case VK_HOME: return moveTo(0);
    case VK_END: return moveTo(count - 1);
    case 'A':
        if (!key.ctrl) {
            return false;
        }
        m_selection.all(count);
        publish();
        return true;
    case VK_RETURN:
        if (m_selection.primary >= 0 && onActivate) {
            onActivate(m_images[static_cast<std::size_t>(m_selection.primary)].index);
        }
        return true;
    case VK_DELETE:
        if (m_selection.primary >= 0 && onDelete) {
            onDelete();
        }
        return true;
    case VK_F2:
        if (m_selection.primary >= 0 && onRename) {
            onRename();
        }
        return true;
    case 'C':
        if (!key.ctrl || m_selection.primary < 0 || !onCopy) {
            return false;
        }
        onCopy();
        return true;
    default: return false;
    }
}

bool EditionTable::onContextMenu(ui::PointF p) {
    const int row = rowAt(p);
    if (row < 0 || !onMenu) {
        return false;
    }
    if (m_selection.contains(row)) {
        m_selection.primary = row;
    } else {
        m_selection.only(row);
    }
    publish();
    return onMenu(m_images[static_cast<std::size_t>(row)].index, p);
}

RectF EditionTable::focusRect() const {
    return m_selection.primary >= 0 && m_selection.primary < static_cast<int>(m_images.size()) ? rowRect(m_selection.primary)
                                                                                                 : bounds();
}

void EditionTable::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const Columns c = columnsFor(b);
    const float px = 1.0f / canvas.scale();
    const float radius = ui::tokens::radius::r3;
    canvas.fillRoundRect(b, radius, Color::BgPanel);

    auto head = [&](std::wstring_view text, float x, float w, ui::TextAlign align = ui::TextAlign::Leading) {
        canvas.drawText(text, {x, b.y, w, kHead}, TypeStyle::Caption, Color::TextTertiary, align);
    };
    head(m_strings.get(Str::ImagesIndex), c.index, kIndex);
    head(m_strings.get(Str::CommonName), c.name, c.nameEnd - c.name);
    if (c.showArch) {
        head(m_strings.get(Str::ImagesArch), c.arch, kArch);
    }
    head(m_strings.get(Str::ImagesBuild), c.build, kBuild);
    if (c.showLang) {
        head(m_strings.get(Str::ImagesLang), c.lang, kLang);
    }
    if (c.showModified) {
        head(m_strings.get(Str::ImagesModified), c.modified, kModified);
    }
    head(m_strings.get(Str::CommonSize), c.size, kSize, ui::TextAlign::Trailing);
    head(m_strings.get(Str::CommonStatus), c.status, kStatus);

    const RectF v = body();
    canvas.pushClip(v);
    for (int i = 0; i < static_cast<int>(m_images.size()); ++i) {
        const RectF row = rowRect(i);
        if (row.bottom() < v.y || row.y > v.bottom()) {
            continue;
        }
        const auto& image = m_images[static_cast<std::size_t>(i)];
        const bool selected = m_selection.contains(i);
        const bool primary = i == m_selection.primary;
        const bool stateRow = m_stateIndex && *m_stateIndex == image.index;
        if (selected) {
            canvas.fillRect(row, Color::AccentSubtle);
            if (primary) {
                canvas.fillRoundRect({row.x, row.y + 8, 2.0f, kRow - 16}, 1.0f, Color::AccentBase);
            }
        } else if (i == m_hoverRow) {
            canvas.fillRect(row, Color::BgHover);
        }
        ui::Checkbox::paintBox(canvas, {c.check, row.y + (kRow - kCheck) / 2}, selected ? ui::CheckState::On : ui::CheckState::Off,
                               i == m_hoverRow);
        canvas.drawText(std::to_wstring(image.index), {c.index, row.y, kIndex, kRow}, TypeStyle::Body, Color::TextTertiary);
        const Color nameInk = m_dimOthers && !stateRow ? Color::TextSecondary : Color::TextPrimary;
        // D-109 (the reference tool: "2 \u2022 Pro   Professional"): the name, then the edition id on the same line.
        const float nameRoom = c.nameEnd - c.name;
        const float nameW = std::min(std::ceil(canvas.text().measure(image.name, TypeStyle::Body)) + 1, nameRoom);
        canvas.drawText(image.name, {c.name, row.y, nameW, kRow}, TypeStyle::Body, nameInk);
        if (!image.editionId.empty() && nameRoom - nameW > 40) {
            canvas.drawText(image.editionId, {c.name + nameW + 12, row.y, nameRoom - nameW - 12, kRow}, TypeStyle::Body,
                            Color::TextTertiary);
        }
        if (c.showArch) {
            canvas.drawText(core::architectureName(image.architecture), {c.arch, row.y, kArch, kRow}, TypeStyle::Body,
                            Color::TextSecondary);
        }
        canvas.drawText(std::format(L"{}.{}", image.build, image.spBuild), {c.build, row.y, kBuild, kRow}, TypeStyle::Mono,
                        Color::TextSecondary);
        if (c.showLang) {
            canvas.drawText(image.defaultLanguage, {c.lang, row.y, kLang, kRow}, TypeStyle::Body, Color::TextSecondary);
        }
        if (c.showModified) {
            // Last change to the image (servicing/commit); creation date is in the inspector.
            canvas.drawText(formatDate(image.modifiedTime ? image.modifiedTime : image.creationTime, m_language),
                            {c.modified, row.y, kModified, kRow}, TypeStyle::Body, Color::TextSecondary);
        }
        canvas.drawText(formatBytes(image.totalBytes, m_language), {c.size, row.y, kSize, kRow}, TypeStyle::Body,
                        Color::TextPrimary, ui::TextAlign::Trailing);

        // Durum: "● Bağlı" (accent), "● Bağlanıyor" (warning), "● Bağlanamaz" (risk), else "—".
        std::wstring status = L"\u2014";
        Color statusInk = Color::TextTertiary;
        bool dot = false;
        if (stateRow && m_state == RowState::Mounted) {
            status = m_strings.get(Str::ImagesMountedState);
            statusInk = Color::AccentText;
            dot = true;
        } else if (stateRow && m_state == RowState::Working) {
            status = m_strings.get(Str::ImagesMountingState);
            statusInk = Color::StatusWarning;
            dot = true;
        } else if (stateRow && m_state == RowState::Failed) {
            status = m_strings.get(Str::ImagesCannotMount);
            statusInk = Color::StatusError;
            dot = true;
        }
        float sx = c.status;
        if (dot) {
            canvas.fillEllipse({sx + 3, row.y + kRow / 2}, 3.0f, statusInk == Color::AccentText ? Color::AccentBase : statusInk);
            sx += 13;
        }
        canvas.drawText(status, {sx, row.y, c.status + kStatus - sx, kRow}, TypeStyle::Small, statusInk);
    }
    canvas.popClip();
    canvas.strokeRoundRect(b, radius, Color::LineSubtle);
    canvas.hairlineH(b.x, b.y + kHead - px, b.width, Color::LineSubtle);
}

} // namespace wl::app
