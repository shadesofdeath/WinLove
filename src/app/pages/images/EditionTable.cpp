#include "app/pages/images/EditionTable.h"

#include "app/Format.h"
#include "ui/widgets/Checkbox.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kRow = 24.0f;
constexpr float kCheck = 28.0f;
constexpr float kIconGap = 6.0f;
constexpr float kEditionId = 150.0f;
constexpr float kModified = 104.0f;
constexpr float kMinName = 260.0f; // optional columns drop out below this name width
constexpr float kArch = 80.0f;
constexpr float kBuild = 120.0f;
constexpr float kLang = 72.0f;
constexpr float kSize = 96.0f;
constexpr float kStatus = 120.0f;
constexpr float kCellPad = 8.0f;

// Right-aligned fixed columns; "Sürüm ID" and "Değiştirilme" are shown only while the name
// column keeps at least kMinName (narrow windows / open inspector drop them, modified first).
struct Columns {
    float name, nameEnd, editionId, arch, build, lang, modified, size, status;
    bool showEditionId, showModified;
};

Columns columnsFor(RectF b) {
    Columns c{};
    c.name = b.x + kCheck;
    const float fixed = kArch + kBuild + kLang + kSize + kStatus;
    const float room = b.right() - c.name - fixed;
    c.showModified = room - kModified - kEditionId >= kMinName;
    c.showEditionId = room - kEditionId >= kMinName;
    c.status = b.right() - kStatus;
    c.size = c.status - kSize;
    c.modified = c.size - (c.showModified ? kModified : 0.0f);
    c.lang = c.modified - kLang;
    c.build = c.lang - kBuild;
    c.arch = c.build - kArch;
    c.editionId = c.arch - (c.showEditionId ? kEditionId : 0.0f);
    c.nameEnd = c.editionId;
    return c;
}
} // namespace

EditionTable::EditionTable(const Localization& strings, Language language) : m_strings(strings), m_language(language) {
    setFocusable(true);
    setAccessible(ui::AccessRole::List, strings.get(Str::ImagesTitle));
}

void EditionTable::setImages(std::vector<core::ImageInfo> images) {
    m_images = std::move(images);
    m_hoverRow = -1;
    invalidate();
}

void EditionTable::setSelected(std::optional<int> index) {
    m_selected = index;
    invalidate();
}

void EditionTable::setRowState(std::optional<int> index, RowState state, bool dimOthers) {
    m_stateIndex = index;
    m_state = state;
    m_dimOthers = dimOthers;
    invalidate();
}

float EditionTable::contentHeight() const noexcept {
    return kRow * static_cast<float>(m_images.size() + 1);
}

RectF EditionTable::rowRect(int row) const {
    const RectF b = bounds();
    return {b.x, b.y + kRow * static_cast<float>(row + 1), b.width, kRow};
}

int EditionTable::rowAt(ui::PointF p) const {
    const RectF b = bounds();
    const int row = static_cast<int>(std::floor((p.y - b.y) / kRow)) - 1;
    return b.contains(p) && row >= 0 && row < static_cast<int>(m_images.size()) ? row : -1;
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

void EditionTable::selectRow(int row) {
    if (row >= 0 && row < static_cast<int>(m_images.size())) {
        const int index = m_images[static_cast<std::size_t>(row)].index;
        if (!m_selected || *m_selected != index) {
            m_selected = index;
            if (onSelect) {
                onSelect(index);
            }
            invalidate();
        }
    }
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
    selectRow(rowAt(p));
}

void EditionTable::onDoubleClick() {
    if (m_selected && onActivate) {
        onActivate(*m_selected);
    }
}

bool EditionTable::onKeyDown(const ui::KeyEvent& key) {
    const int count = static_cast<int>(m_images.size());
    if (count == 0) {
        return false;
    }
    const int current = std::max(rowOfIndex(m_selected), 0);
    switch (key.virtualKey) {
    case VK_UP: selectRow(std::max(current - 1, 0)); return true;
    case VK_DOWN: selectRow(std::min(current + 1, count - 1)); return true;
    case VK_HOME: selectRow(0); return true;
    case VK_END: selectRow(count - 1); return true;
    case VK_RETURN:
        if (m_selected && onActivate) {
            onActivate(*m_selected);
        }
        return true;
    default: return false;
    }
}

RectF EditionTable::focusRect() const {
    const int row = rowOfIndex(m_selected);
    return row >= 0 ? rowRect(row) : bounds();
}

void EditionTable::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    const Columns c = columnsFor(b);
    const float px = 1.0f / canvas.scale();

    auto head = [&](std::wstring_view text, float x, float w, ui::TextAlign align = ui::TextAlign::Leading) {
        canvas.drawText(text, {x, b.y, w, kRow}, TypeStyle::Caption, Color::TextTertiary, align);
    };
    head(m_strings.get(Str::ImagesIndex) + L" · " + m_strings.get(Str::CommonName), c.name, c.nameEnd - c.name);
    if (c.showEditionId) {
        head(m_strings.get(Str::ImagesEditionId), c.editionId, kEditionId);
    }
    head(m_strings.get(Str::ImagesArch), c.arch, kArch);
    head(m_strings.get(Str::ImagesBuild), c.build, kBuild);
    head(m_strings.get(Str::ImagesLang), c.lang, kLang);
    if (c.showModified) {
        head(m_strings.get(Str::ImagesModified), c.modified, kModified);
    }
    head(m_strings.get(Str::CommonSize), c.size, kSize - kCellPad, ui::TextAlign::Trailing);
    head(m_strings.get(Str::CommonStatus), c.status, kStatus);
    canvas.hairlineH(b.x, b.y + kRow - px, b.width, Color::LineSubtle);

    for (int i = 0; i < static_cast<int>(m_images.size()); ++i) {
        const auto& image = m_images[static_cast<std::size_t>(i)];
        const RectF row = rowRect(i);
        const bool selected = m_selected && *m_selected == image.index;
        const bool stateRow = m_stateIndex && *m_stateIndex == image.index;
        if (selected) {
            canvas.fillRoundRect(row, ui::tokens::radius::r2, Color::AccentSubtle);
        } else if (i == m_hoverRow) {
            canvas.fillRoundRect(row, ui::tokens::radius::r2, Color::BgRaised);
        }
        canvas.hairlineH(row.x, row.bottom() - px, row.width, Color::LineSubtle);

        ui::Checkbox::paintBox(canvas, {row.x + 2, row.y + 6}, selected ? ui::CheckState::On : ui::CheckState::Off,
                               i == m_hoverRow);
        const Color nameInk = m_dimOthers && !stateRow ? Color::TextSecondary : Color::TextPrimary;
        canvas.drawIcon(ui::icons::Icon::LayersEditions, {c.name, row.y + 4}, Color::TextSecondary);
        const float nameX = c.name + ui::tokens::size::icon + kIconGap;
        canvas.drawText(std::format(L"{} · {}", image.index, image.name), {nameX, row.y, c.nameEnd - nameX - kCellPad, kRow},
                        selected ? TypeStyle::BodyStrong : TypeStyle::Body, nameInk);
        if (c.showEditionId) {
            canvas.drawText(image.editionId.empty() ? L"—" : image.editionId,
                            {c.editionId, row.y, kEditionId - kCellPad, kRow}, TypeStyle::Body, Color::TextSecondary);
        }
        canvas.drawText(core::architectureName(image.architecture), {c.arch, row.y, kArch, kRow}, TypeStyle::Body,
                        Color::TextPrimary);
        canvas.drawText(std::format(L"{}.{}", image.build, image.spBuild), {c.build, row.y, kBuild, kRow}, TypeStyle::Mono,
                        Color::TextPrimary);
        canvas.drawText(image.defaultLanguage, {c.lang, row.y, kLang, kRow}, TypeStyle::Body, Color::TextPrimary);
        if (c.showModified) {
            // Last change to the image (servicing/commit); creation date is in the inspector.
            canvas.drawText(formatDate(image.modifiedTime ? image.modifiedTime : image.creationTime, m_language),
                            {c.modified, row.y, kModified - kCellPad, kRow}, TypeStyle::Mono, Color::TextSecondary);
        }
        canvas.drawText(formatBytes(image.totalBytes, m_language), {c.size, row.y, kSize - kCellPad, kRow}, TypeStyle::Mono,
                        Color::TextPrimary, ui::TextAlign::Trailing);

        // Status: the operation/mount/failure of this row wins over "Seçili".
        std::wstring status;
        Color statusInk = Color::TextSecondary;
        if (stateRow && m_state == RowState::Mounted) {
            status = m_strings.get(Str::ImagesMountedState);
            statusInk = Color::StatusSuccess;
        } else if (stateRow && m_state == RowState::Working) {
            status = m_strings.get(Str::ImagesMountingState);
        } else if (stateRow && m_state == RowState::Failed) {
            status = m_strings.get(Str::ImagesCannotMount);
            statusInk = Color::StatusError;
        } else if (selected) {
            status = m_strings.get(Str::ImagesSelectedState);
        }
        if (!status.empty()) {
            canvas.drawText(status, {c.status + kCellPad, row.y, kStatus - kCellPad, kRow}, TypeStyle::Caption, statusInk);
        }
    }
}

} // namespace wl::app
