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
namespace tk = ui::tokens;

namespace {
constexpr float kLine = RecentList::kNameLine;      // a one-line row's text, the meta at its right
constexpr float kSummaryLine = RecentList::kSummaryLine;
constexpr float kIcon = tk::size::iconLarge; // the row's icon
constexpr float kRemove = tk::size::control; // the x: a 24 icon button
constexpr float kMetaGap = tk::spacing::s6;  // the name stops this far before the meta
constexpr float kRemoveGap = tk::spacing::s4; // meta to the x
constexpr float kTextX = RecentList::kTextX;
constexpr float kMinRows = 1.0f; // the empty list keeps a row for its note

ui::icons::Icon iconFor(const std::wstring& format) {
    if (format == L"ISO") return ui::icons::Icon::DiscIso;
    if (format == L"Folder") return ui::icons::Icon::Folder;
    if (format == L"VHD" || format == L"VHDX") return ui::icons::Icon::DiskPartition; // D-141: an installed Windows
    return ui::icons::Icon::ImageWim;
}
} // namespace

RecentList::RecentList(const Localization& strings, Language language) : m_strings(strings), m_language(language) {
    setFocusable(false); // until there is a row to be on
    setAccessible(ui::AccessRole::List, strings.get(Str::SourceRecent));
    m_bar = &add<ui::ScrollBar>();
    m_bar->onScroll = [this](float offset) {
        m_offset = offset;
        invalidate();
    };
}

RecentList::~RecentList() {
    *m_alive = false;
}

void RecentList::setEntries(std::vector<RecentSource> entries) {
    m_rows.clear();
    m_rows.reserve(entries.size());
    for (auto& e : entries) {
        Row row;
        std::error_code ec;
        row.exists = std::filesystem::exists(e.path, ec);
        const std::wstring file = e.path.filename().wstring();
        row.name = file.empty() ? e.path.wstring() : file;
        row.summary = std::move(e.summary);
        row.meta = (e.format == L"Folder" ? m_strings.get(Str::SourceFolderType) : e.format) + L" · " +
                   formatBytes(e.size, m_language) + L" · " + formatRecentTime(e.lastOpened, m_language, m_strings);
        row.icon = iconFor(e.format);
        row.where = e.path.parent_path().wstring();
        row.path = std::move(e.path);
        m_rows.push_back(std::move(row));
    }
    const int n = count();
    m_selected = n == 0 ? -1 : std::clamp(m_selected, 0, n - 1);
    m_hoverRow = -1;
    m_hoverRemove = false;
    m_downRemove = -1;
    m_onRemove = false;
    m_menuRow = -1;
    m_measured = false; // new texts: layout() measures them
    m_hoverFade.snap(-1); // other rows now: nothing of the old highlight fades out over them
    setFocusable(n > 0);
    layout();
    invalidate();
}

float RecentList::contentHeight() const noexcept {
    return std::max(static_cast<float>(m_rows.size()), kMinRows) * kRow;
}

bool RecentList::scrolls() const noexcept {
    return static_cast<float>(m_rows.size()) * kRow > bounds().height + 0.5f;
}

float RecentList::maxScroll() const noexcept {
    return std::max(static_cast<float>(m_rows.size()) * kRow - bounds().height, 0.0f);
}

float RecentList::contentRight() const noexcept {
    // A scrolling list keeps the bar's room: the x must not sit under its thumb.
    return bounds().right() - (scrolls() ? ui::ScrollBar::kWidth : 0.0f);
}

void RecentList::layout() {
    const RectF b = bounds();
    m_bar->setBounds({b.right() - ui::ScrollBar::kWidth, b.y, ui::ScrollBar::kWidth, b.height});
    m_bar->setRange(static_cast<float>(m_rows.size()) * kRow, b.height);
    m_offset = std::clamp(m_offset, 0.0f, maxScroll());
    m_bar->setOffset(m_offset);
    // Measured here, not while painting, and once per set of entries: the page lays this out again
    // on every step of its own scrolling. Before the page has a host only the fallbacks are known.
    if (m_measured) {
        return;
    }
    m_noteWidth = std::ceil(textWidth(m_strings.get(Str::SourceFileMissing), TypeStyle::Caption, 120.0f));
    for (auto& row : m_rows) {
        row.metaWidth = row.exists ? std::ceil(textWidth(row.meta, TypeStyle::Caption, 180.0f)) : m_noteWidth;
    }
    m_measured = host() != nullptr;
}

RectF RecentList::rowRect(int index) const {
    const RectF b = bounds();
    return {b.x, std::round(b.y + static_cast<float>(index) * kRow - m_offset), b.width, kRow};
}

int RecentList::rowAt(ui::PointF p) const {
    const RectF b = bounds();
    if (!b.contains(p) || m_rows.empty()) {
        return -1;
    }
    const int row = static_cast<int>(std::floor((p.y - b.y + m_offset) / kRow));
    return row >= 0 && row < count() ? row : -1;
}

RectF RecentList::removeRect(int index) const {
    // The x's glyph ends kInset before the row's end, mirroring the icon at its start.
    const RectF row = rowRect(index);
    return {contentRight() - kInset + (kRemove - tk::size::icon) / 2 - kRemove, row.y + std::round((kRow - kRemove) / 2), kRemove,
            kRemove};
}

bool RecentList::missing(int index) const {
    return index >= 0 && index < count() && !m_rows[static_cast<std::size_t>(index)].exists;
}

bool RecentList::removeShown(int index) const {
    if (index < 0 || index >= count()) {
        return false;
    }
    // A missing file's entry can only be removed: its x stays. The others show it when they are the
    // row in hand (pointer, or the keyboard's selection).
    return missing(index) || index == m_hoverRow || (focused() && index == m_selected);
}

void RecentList::setOffset(float offset) {
    m_offset = std::clamp(offset, 0.0f, maxScroll());
    m_bar->setOffset(m_offset);
    invalidate();
}

void RecentList::reveal(int index) {
    const float top = static_cast<float>(index) * kRow;
    const float height = bounds().height;
    if (top < m_offset) {
        setOffset(top);
    } else if (top + kRow > m_offset + height) {
        setOffset(top + kRow - height);
    }
}

void RecentList::remove(int index) {
    if (index >= 0 && index < count() && onRemove) {
        // Copies: the handler changes the list, and with it m_rows (and maybe the handler's owner).
        const std::filesystem::path path = m_rows[static_cast<std::size_t>(index)].path;
        const auto handler = onRemove;
        handler(path);
    }
}

void RecentList::select(int index) {
    if (index >= 0 && index < count()) {
        if (index != m_selected) {
            m_selected = index;
            m_onRemove = false;
        }
        reveal(index);
        invalidate();
    }
}

void RecentList::openSelected() {
    if (m_selected >= 0 && m_selected < count() && onOpen) {
        const std::filesystem::path path = m_rows[static_cast<std::size_t>(m_selected)].path;
        const auto handler = onOpen;
        handler(path);
    }
}

void RecentList::setHover(int row, bool overRemove) {
    if (row == m_hoverRow && overRemove == m_hoverRemove) {
        return;
    }
    // The x says what it does (and its key); a row says where its file is (the name alone does not).
    setTooltip(row < 0      ? std::wstring{}
               : overRemove ? m_strings.get(Str::SourceRemoveTip)
                            : m_rows[static_cast<std::size_t>(row)].where);
    m_hoverRow = row;
    m_hoverRemove = overRemove;
    if (m_hoverFade.set(row)) {
        animate();
    }
    invalidate();
}

void RecentList::onPointerMove(ui::PointF p) {
    const int row = rowAt(p);
    setHover(row, row >= 0 && removeRect(row).contains(p)); // the row under the pointer shows its x
}

void RecentList::onHoverChanged(bool hovered) {
    if (!hovered) {
        setHover(-1, false);
    }
    invalidate();
}

void RecentList::onFocusChanged(bool focused) {
    if (!focused) {
        m_onRemove = false;
    }
    invalidate();
}

bool RecentList::tick(double now) {
    const bool running = m_hoverFade.tick(now);
    invalidate();
    return running;
}

void RecentList::onPointerDown(ui::PointF p) {
    const int row = rowAt(p);
    // The press point is what counts, even if no move came before it (a click right after a scroll).
    setHover(row, row >= 0 && removeRect(row).contains(p));
    m_downRemove = m_hoverRemove ? row : -1;
    select(row);
}

void RecentList::onClick() {
    if (const int row = std::exchange(m_downRemove, -1); row >= 0) {
        if (m_hoverRemove && m_hoverRow == row) { // released on the same x
            remove(row);
        }
        return;
    }
    if (m_hoverRow >= 0 && m_hoverRow == m_selected) {
        openSelected(); // a row opens with one click, as the cards did
    }
}

void RecentList::onDoubleClick() {} // the first click already opened it

bool RecentList::onKeyDown(const ui::KeyEvent& key) {
    const int n = count();
    if (n == 0) {
        return false;
    }
    const int page = std::max(static_cast<int>(bounds().height / kRow) - 1, 1);
    switch (key.virtualKey) {
    case VK_UP: select(std::max(m_selected - 1, 0)); return true;
    case VK_DOWN: select(std::min(m_selected + 1, n - 1)); return true;
    case VK_PRIOR: select(std::max(m_selected - page, 0)); return true;
    case VK_NEXT: select(std::min(m_selected + page, n - 1)); return true;
    case VK_HOME: select(0); return true;
    case VK_END: select(n - 1); return true;
    case VK_RIGHT:
        if (m_selected >= 0 && !m_onRemove) {
            m_onRemove = true; // onto the row's x
            reveal(m_selected);
            invalidate();
        }
        return true;
    case VK_LEFT:
        if (m_onRemove) {
            m_onRemove = false;
            invalidate();
        }
        return true;
    case VK_RETURN:
    case VK_SPACE:
        if (m_onRemove) {
            remove(m_selected);
        } else if (key.virtualKey == VK_RETURN) {
            openSelected();
        } else {
            return false;
        }
        return true;
    case VK_DELETE: remove(m_selected); return true;
    default: return false;
    }
}

bool RecentList::onWheel(ui::PointF p, float lines) {
    if (!scrolls()) {
        return false; // the page itself may scroll
    }
    setOffset(m_offset - lines * kRow);
    onPointerMove(p); // no move follows a wheel: the row under the pointer changed
    return true;
}

bool RecentList::onContextMenu(ui::PointF p) {
    const int row = rowAt(p);
    if (row < 0 || !host()) {
        return false;
    }
    select(row);
    const std::filesystem::path path = m_rows[static_cast<std::size_t>(row)].path;
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
        // The row the menu is about stays lit while it is open. The menu closes from its destructor
        // too - after this list is gone when the page goes first: `alive` says so.
        [this, alive = m_alive] {
            if (*alive) {
                m_menuRow = -1;
                invalidate();
            }
        });
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false, /*owner=*/this);
    m_menuRow = row;
    invalidate();
    return true;
}

RectF RecentList::focusRect() const {
    // On the x: its button's own ring. On a row: the Host's ring drawn just inside the row (as a
    // table's), so the card and the neighbouring rows neither cut nor cover it.
    const RectF b = bounds();
    const float ring = tk::radius::focusOffset + 1;
    if (m_selected >= 0) {
        const RectF row = rowRect(m_selected);
        if (row.y >= b.y - 1 && row.bottom() <= b.bottom() + 1) {
            return m_onRemove ? removeRect(m_selected) : row.inset(ring, ring);
        }
    }
    return b.width > 2 * ring && b.height > 2 * ring ? b.inset(ring, ring) : b;
}

float RecentList::focusRadius() const {
    return m_onRemove ? tk::radius::button : 0.0f;
}

RectF RecentList::tooltipAnchor() const {
    // Under the x, or under the row's name (the folder it is in reads on from it).
    if (m_hoverRow < 0 || m_hoverRow >= count()) {
        return bounds();
    }
    if (m_hoverRemove) {
        return removeRect(m_hoverRow);
    }
    const RectF row = rowRect(m_hoverRow);
    return {row.x + kTextX, row.y, std::max(row.width - kTextX, 0.0f), row.height};
}

ui::Cursor RecentList::cursor() const {
    return m_hoverRow >= 0 ? ui::Cursor::Hand : ui::Cursor::Arrow; // a row opens with one click
}

void RecentList::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    if (m_rows.empty()) {
        // Nothing opened yet: one quiet line where the first row will be.
        const float y = b.y + std::round((kRow - kLine) / 2);
        canvas.drawIcon(ui::icons::Icon::QueueClock, {b.x + kInset + (kIcon - tk::size::icon) / 2, b.y + std::round((kRow - tk::size::icon) / 2)},
                        Color::TextTertiary);
        canvas.drawText(m_strings.get(Str::SourceRecentEmpty), {b.x + kTextX, y, std::max(b.width - kTextX - kInset, 0.0f), kLine},
                        TypeStyle::Caption, Color::TextTertiary);
        return;
    }
    canvas.pushClip(b);
    const int first = std::max(static_cast<int>(m_offset / kRow), 0);
    const int last = std::min(static_cast<int>((m_offset + b.height) / kRow) + 1, count() - 1);
    for (int i = first; i <= last; ++i) {
        const Row& row = m_rows[static_cast<std::size_t>(i)];
        const RectF r = rowRect(i);
        const bool selected = i == m_selected && focused();
        const float hover = i == m_menuRow ? 1.0f : m_hoverFade.amount(i);
        if (selected) { // as a table's selected row: accent.subtle and the 2px accent bar
            canvas.fillRect(r, Color::AccentSubtle);
            const float inset = std::min(10.0f, kRow / 4);
            canvas.fillRoundRect({r.x, r.y + inset, 2.0f, kRow - 2 * inset}, 1.0f, Color::AccentBase);
        } else if (hover > 0) {
            canvas.fillRect(r, ui::Ink(Color::BgHover, Color::BgHover, 0.0f, hover));
        }

        const Color ink = row.exists ? Color::TextPrimary : Color::TextDisabled;
        const Color soft = row.exists ? Color::TextSecondary : Color::TextDisabled;
        canvas.drawIcon(row.icon, {r.x + kInset, r.y + std::round((kRow - kIcon) / 2)}, row.exists ? Color::AccentBase : Color::TextDisabled,
                        ui::IconVariant::Regular16, kIcon);

        // Right to left: the x's room, the meta (or the missing note), then what is left for the name.
        const RectF x = removeRect(i);
        const float metaRight = x.x - kRemoveGap;
        const float metaX = metaRight - row.metaWidth;
        const float textX = r.x + kTextX;
        const float textW = std::max(metaX - kMetaGap - textX, 0.0f);
        const float lineY = r.y + std::round((kRow - kLine) / 2);
        if (row.exists) {
            canvas.drawText(row.meta, {metaX, lineY, row.metaWidth + 1, kLine}, TypeStyle::Caption, Color::TextTertiary);
        } else {
            canvas.drawText(m_strings.get(Str::SourceFileMissing), {metaX, lineY, row.metaWidth + 1, kLine}, TypeStyle::Caption,
                            Color::StatusWarning);
        }
        if (row.summary.empty()) {
            canvas.drawText(row.name, {textX, lineY, textW, kLine}, TypeStyle::Body, ink);
        } else {
            const float top = r.y + std::round((kRow - kLine - kSummaryLine) / 2);
            canvas.drawText(row.name, {textX, top, textW, kLine}, TypeStyle::Body, ink);
            canvas.drawText(row.summary, {textX, top + kLine, textW, kSummaryLine}, TypeStyle::Caption, soft);
        }

        // The x: a quiet icon button. A missing entry shows it at rest; the others fade it in with
        // their hover, or show it while selected with the keyboard.
        const float shown = !row.exists || selected ? 1.0f : hover;
        if (shown > 0) {
            const bool hot = (i == m_hoverRow && m_hoverRemove) || (selected && m_onRemove);
            if (hot) {
                canvas.fillRoundRect(x, tk::radius::button, Color::BgHover);
            }
            canvas.drawIcon(ui::icons::Icon::Close, {x.x + (kRemove - tk::size::icon) / 2, x.y + (kRemove - tk::size::icon) / 2},
                            ui::Ink(hot ? Color::TextPrimary : Color::TextTertiary, hot ? Color::TextPrimary : Color::TextTertiary, 0.0f,
                                    shown));
        }
    }
    canvas.popClip();
}

} // namespace wl::app
