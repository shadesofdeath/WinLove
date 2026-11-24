#include "app/pages/SourcePage.h"

#include "app/pages/PageBits.h"
#include "ui/widgets/FormView.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;
namespace tk = ui::tokens;

namespace {
// 2026-11-10: the page in the rhythm of the other pages' forms — a section is a title block of
// FormView::kSection, the bars sit a spacing step apart.
constexpr float kGap = tk::spacing::s4;           // drop row ↔ info bars
constexpr float kInfoBarHeight = ui::InfoBar::kHeight;
constexpr float kSection = ui::FormView::kSection; // a section's title block (paintFormSection)
constexpr float kHintGap = tk::spacing::s2;       // the mounts table ↔ its hint line
constexpr float kLine = tk::kTypeStyles[static_cast<std::size_t>(TypeStyle::Caption)].lineHeight; // the hint line
constexpr int kMountRows = 5;  // more scroll inside the table
constexpr float kListMinRows = 3; // fewer than this would not read as a list: the page scrolls then
// The mounts table's cells are padded TableView::kCellPad: shifted so that its text starts where the
// recent rows' icons and the section titles do.
constexpr float kTableShift = RecentList::kInset - ui::TableView::kCellPad;
constexpr float kStateColumn = 168.0f; // "Yeniden bağlanacak", "İmaj değil (dokunulmaz)"
} // namespace

SourcePage::SourcePage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_intents(std::move(intents)) {
    // Children in the order they are seen: Tab walks them top to bottom.
    m_drop = &add<DropRow>(strings.get(Str::SourceDropTitle), strings.get(Str::SourceDropHint), strings.get(Str::SourceDropUnsupported),
                           strings.get(Str::SourceLoading));
    m_drop->onInvoke = [this] {
        if (m_intents.pickFile) {
            m_intents.pickFile();
        }
    };
    m_error = &add<ui::InfoBar>(ui::InfoKind::Error, strings.get(Str::SourceOpenFailed), L"", strings.get(Str::CommonClose));
    m_error->setVisible(false);
    m_error->onClose = [this] {
        m_error->setVisible(false);
        layout();
    };
    m_host = &add<ui::InfoBar>(ui::InfoKind::Warning, strings.get(Str::SourceHostTitle), L"", strings.get(Str::CommonClose));
    m_host->setVisible(false);
    m_host->onClose = [this] {
        m_host->setVisible(false);
        layout();
    };
    m_mounts = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::SourceMountedFolder), 300},
        {strings.get(Str::SourceMountedImage), 260},
        {strings.get(Str::SourceMountedFile), 0},
        {strings.get(Str::SourceMountedState), kStateColumn},
    });
    m_mounts->setAccessible(ui::AccessRole::Group, strings.get(Str::SourceMounted));
    m_mounts->paintCell = [this](ui::Canvas& c, int row, int column, RectF rect, ui::TableView::CellState cell) {
        paintMountCell(c, row, column, rect, cell.selected);
    };
    m_mounts->onActivate = [this](int row) {
        if (row >= 0 && row < static_cast<int>(m_mountRows.size()) && m_intents.adoptMount) {
            m_intents.adoptMount(m_mountRows[static_cast<std::size_t>(row)].folder);
        }
    };
    m_mounts->onKey = [this](const ui::KeyEvent& key) {
        const int row = m_mounts->selected();
        if (key.virtualKey == VK_DELETE && row >= 0 && row < static_cast<int>(m_mountRows.size()) && m_intents.discardMount) {
            const auto& m = m_mountRows[static_cast<std::size_t>(row)];
            m_intents.discardMount(m.folder, m.imageName);
            return true;
        }
        return false;
    };
    // What the cells may clip, whole: the image, then its file (a tooltip is one line).
    m_mounts->tooltipFor = [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_mountRows.size())) {
            return std::wstring{};
        }
        const auto& m = m_mountRows[static_cast<std::size_t>(row)];
        const std::wstring& label = m_mountLabels[static_cast<std::size_t>(row)];
        const std::wstring file = m.record ? m.record->imagePath.wstring() : m.folder.wstring();
        return label.empty() ? file : label + L"  \u00b7  " + file;
    };
    m_recent = &add<RecentList>(strings, language);
    m_recent->onOpen = [this](const std::filesystem::path& path) {
        if (m_intents.openPath) {
            m_intents.openPath(path);
        }
    };
    m_recent->onRemove = [this](const std::filesystem::path& path) {
        if (m_intents.removePath) {
            m_intents.removePath(path);
        }
    };
    m_recent->onVerifyHash = [this](const std::filesystem::path& path) {
        if (m_intents.verifyHash) {
            m_intents.verifyHash(path);
        }
    };
    m_recent->onShowInFolder = [this](const std::filesystem::path& path) {
        if (m_intents.showInFolder) {
            m_intents.showInFolder(path);
        }
    };
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Recent) {
            refreshRecent();
        }
        if (change == AppState::Change::SystemMounts || change == AppState::Change::Mount) {
            refreshMounts();
        }
        if (change == AppState::Change::Host) {
            refreshHost();
        }
        if (change == AppState::Change::Operation) {
            invalidate(); // a row being taken over says so (paintMountCell)
        }
    });
    refreshRecent();
    refreshMounts();
    refreshHost();
}

SourcePage::~SourcePage() {
    m_state.unsubscribe(m_subscription);
}

void SourcePage::refreshRecent() {
    m_recent->setEntries(m_state.recent().entries());
    layout();
}

void SourcePage::refreshMounts() {
    const auto& mounts = m_state.systemMounts();
    m_mountRows = mounts ? mounts->items : std::vector<core::MountCheck>{};
    // "Windows 11 Pro · #6": formed here, not while painting.
    m_mountLabels.clear();
    for (const auto& m : m_mountRows) {
        std::wstring label = m.imageName;
        if (m.record) {
            label = (label.empty() ? std::wstring() : label + L"  ·  ") + L"#" + std::to_wstring(m.record->index);
        }
        m_mountLabels.push_back(std::move(label));
    }
    m_mounts->setVisible(!m_mountRows.empty());
    m_mounts->setRowCount(static_cast<int>(m_mountRows.size()));
    m_mounts->refresh();
    layout();
    invalidate();
}

void SourcePage::refreshHost() {
    const auto& report = m_state.hostDism();
    if (!report || report->healthy()) {
        m_host->setVisible(false);
        layout();
        return;
    }
    const auto& first = report->problems.front();
    const Str what = first.what == L"missing" || first.what == L"service missing" ? Str::SourceHostMissing
                     : first.what == L"service disabled"                           ? Str::SourceHostDisabled
                                                                                    : Str::SourceHostUnsigned;
    const std::wstring more = report->problems.size() > 1
                                  ? m_strings.format(Str::SourceHostMore, {{L"n", std::to_wstring(report->problems.size() - 1)}})
                                  : std::wstring();
    const Str action = report->usingAdk ? Str::SourceHostUsingAdk
                       : report->adk    ? Str::SourceHostAdkInstalled
                                        : Str::SourceHostInstallAdk;
    const std::wstring file = std::filesystem::path(first.file).filename().wstring();
    const std::wstring body = m_strings.format(Str::SourceHostBody, {{L"file", file.empty() ? first.file : file},
                                                                     {L"what", m_strings.get(what)},
                                                                     {L"more", more},
                                                                     {L"action", m_strings.get(action)}});
    m_host->set(ui::InfoKind::Warning, m_strings.get(Str::SourceHostTitle), body);
    m_host->setTooltip(body); // one line on the bar: the whole of it on hover
    m_host->setVisible(true);
    layout();
}

void SourcePage::paintMountCell(ui::Canvas& canvas, int row, int column, RectF rect, bool selected) {
    if (row < 0 || row >= static_cast<int>(m_mountRows.size())) {
        return;
    }
    const auto& m = m_mountRows[static_cast<std::size_t>(row)];
    switch (column) {
    // Paths in the UI face like the recent rows' (Mono sat on another baseline than the row's text).
    case 0: canvas.drawText(m.folder.native(), rect, TypeStyle::Body, Color::TextPrimary); break;
    case 1: {
        const std::wstring& label = m_mountLabels[static_cast<std::size_t>(row)];
        canvas.drawText(label.empty() ? std::wstring_view(L"—") : std::wstring_view(label), rect,
                        selected ? TypeStyle::BodyStrong : TypeStyle::Body, label.empty() ? Color::TextTertiary : Color::TextPrimary);
        break;
    }
    case 2:
        if (m.record) {
            canvas.drawText(m.record->imagePath.native(), rect, TypeStyle::Caption, Color::TextSecondary);
        } else {
            canvas.drawText(L"—", rect, TypeStyle::Caption, Color::TextTertiary);
        }
        break;
    case 3: {
        const auto& mounted = m_state.mounted();
        const bool inUse = mounted && _wcsicmp(mounted->mountDir.c_str(), m.folder.c_str()) == 0;
        // 2026-11-11: the row being taken over says what is done to it — "Devralınıyor", or
        // "Yeniden bağlanıyor" while DISM remounts it (no percentage: neither has one).
        if (const auto& op = m_state.operation();
            op && (op->kind == EngineOperation::Kind::Adopting || op->kind == EngineOperation::Kind::Remounting) &&
            _wcsicmp(op->path.c_str(), m.folder.c_str()) == 0) {
            const Color working = Color::AccentText;
            canvas.fillEllipse({rect.x + 3, rect.y + rect.height / 2}, tk::size::dot / 2, working);
            canvas.drawText(m_strings.get(op->kind == EngineOperation::Kind::Remounting ? Str::StatusRemounting : Str::StatusAdopting),
                            {rect.x + 13, rect.y, std::max(rect.width - 13, 0.0f), rect.height}, TypeStyle::Small, working);
            break;
        }
        Str state = Str::SourceMountedOk;
        Color ink = Color::StatusSuccess;
        switch (m.state) {
        case core::MountState::Ok:
            state = inUse ? Str::SourceMountedInUse : m.record && m.record->readOnly ? Str::SourceMountedReadOnly : Str::SourceMountedOk;
            ink = inUse ? Color::AccentText : Color::StatusSuccess;
            break;
        case core::MountState::NeedsRemount: state = Str::SourceMountedRemount; ink = Color::StatusWarning; break;
        case core::MountState::ImageMissing: state = Str::SourceMountedMissing; ink = Color::StatusError; break;
        case core::MountState::Orphaned: state = Str::SourceMountedOrphan; ink = Color::StatusWarning; break;
        case core::MountState::Foreign: state = Str::SourceMountedForeign; ink = Color::StatusWarning; break;
        default: state = Str::SourceMountedInvalid; ink = Color::StatusError; break;
        }
        // A status dot and its word, as the risk cells of the other tables.
        canvas.fillEllipse({rect.x + 3, rect.y + rect.height / 2}, tk::size::dot / 2, ink);
        canvas.drawText(m_strings.get(state), {rect.x + 13, rect.y, std::max(rect.width - 13, 0.0f), rect.height}, TypeStyle::Small, ink);
        break;
    }
    default: break;
    }
}

void SourcePage::setLoading(bool loading) {
    m_drop->setLoading(loading);
    if (loading) {
        m_error->setVisible(false);
        layout();
    }
}

void SourcePage::showError(const std::wstring& message) {
    m_error->set(ui::InfoKind::Error, m_strings.get(Str::SourceOpenFailed), message);
    m_error->setTooltip(message);
    m_error->setVisible(true);
    layout();
}

void SourcePage::setDragState(ui::DropZone::DragState state) {
    m_drop->setDragState(state);
}

void SourcePage::layout() {
    const RectF b = bounds();
    const float before = m_content;
    float y = b.y;
    m_drop->setBounds({b.x, y, b.width, DropRow::kHeight});
    y += DropRow::kHeight;
    for (ui::InfoBar* bar : {m_error, m_host}) {
        if (bar->visible()) {
            y += kGap;
            bar->setBounds({b.x, y, b.width, kInfoBarHeight});
            y += kInfoBarHeight;
        }
    }
    if (m_mounts->visible()) {
        m_mountsTitle = {b.x + RecentList::kInset, y, std::max(b.width - RecentList::kInset, 0.0f), kSection};
        y += kSection;
        // The columns share the width (the folder and the image keep the most of it); the WIM file's
        // path takes what is left, the state its word.
        const float width = std::max(b.width - kTableShift, 0.0f);
        const float flexible = std::max(width - kStateColumn, 0.0f);
        m_mounts->setColumnWidth(0, std::floor(flexible * 0.32f));
        m_mounts->setColumnWidth(1, std::floor(flexible * 0.34f));
        const int rows = std::min(static_cast<int>(m_mountRows.size()), kMountRows);
        const float h = ui::TableView::kHeader + ui::TableView::kRow * static_cast<float>(rows) + 1;
        m_mounts->setBounds({b.x + kTableShift, y, width, h});
        y += h + kHintGap;
        m_mountsHint = {b.x + RecentList::kInset, y, std::max(b.width - RecentList::kInset, 0.0f), kLine};
        y += kLine;
    }
    m_recentTitle = {b.x + RecentList::kInset, y, std::max(b.width - RecentList::kInset, 0.0f), kSection};
    y += kSection;
    // The list takes the rest of the card (it scrolls in it); the page scrolls only when not even a
    // few rows fit — at the minimum window with every bar shown.
    const float want = m_recent->contentHeight();
    const float least = std::min(want, kListMinRows * RecentList::kRow);
    const float room = b.bottom() - y;
    const float h = std::clamp(room, least, want);
    m_recent->setBounds({b.x, y, b.width, h});
    m_content = y + least - b.y;
    contentChanged(*this, before);
}

void SourcePage::paint(ui::Canvas& canvas) {
    if (m_mounts->visible()) {
        paintFormSection(canvas, m_mountsTitle, m_strings.get(Str::SourceMounted));
        canvas.drawText(m_strings.get(Str::SourceMountedHint), m_mountsHint, TypeStyle::Caption, Color::TextTertiary);
    }
    paintFormSection(canvas, m_recentTitle, m_strings.get(Str::SourceRecent));
}

} // namespace wl::app
