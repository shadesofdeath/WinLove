#include "app/pages/SourcePage.h"

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
// Offsets under the page description, from screen 01 (content top 88).
constexpr float kTopGap = 20.0f;
constexpr float kZoneHeight = 160.0f;
constexpr float kGap = 16.0f;
constexpr float kSectionGap = 24.0f;
constexpr float kSectionLine = 16.0f;
constexpr float kSectionToTable = 8.0f;
constexpr float kInfoBarHeight = 32.0f;
} // namespace

SourcePage::SourcePage(AppState& state, const Localization& strings, Language language, Intents intents)
    : m_state(state), m_strings(strings), m_intents(std::move(intents)) {
    m_drop = &add<ui::DropZone>(strings.get(Str::SourceDropTitle), strings.get(Str::SourceDropHint),
                                strings.get(Str::SourceDropUnsupported), strings.get(Str::SourceLoading));
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
    m_mounts = &add<ui::TableView>(std::vector<ui::TableColumn>{
        {strings.get(Str::SourceMountedFolder), 300},
        {strings.get(Str::SourceMountedImage), 240},
        {strings.get(Str::SourceMountedFile), 0},
        {strings.get(Str::SourceMountedState), 170},
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
    m_recent->setVisible(!m_recent->empty());
    layout();
}

void SourcePage::refreshMounts() {
    const auto& mounts = m_state.systemMounts();
    m_mountRows = mounts ? mounts->items : std::vector<core::MountCheck>{};
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
    m_host->set(ui::InfoKind::Warning, m_strings.get(Str::SourceHostTitle),
                m_strings.format(Str::SourceHostBody, {{L"file", file.empty() ? first.file : file},
                                                       {L"what", m_strings.get(what)},
                                                       {L"more", more},
                                                       {L"action", m_strings.get(action)}}));
    m_host->setVisible(true);
    layout();
}

void SourcePage::paintMountCell(ui::Canvas& canvas, int row, int column, RectF rect, bool selected) {
    if (row < 0 || row >= static_cast<int>(m_mountRows.size())) {
        return;
    }
    const auto& m = m_mountRows[static_cast<std::size_t>(row)];
    switch (column) {
    case 0: canvas.drawText(m.folder.wstring(), rect, TypeStyle::Mono, Color::TextPrimary); break;
    case 1: {
        std::wstring name = m.imageName;
        if (m.record) {
            name = (name.empty() ? std::wstring() : name + L"  \u00b7  ") + L"#" + std::to_wstring(m.record->index);
        }
        canvas.drawText(name.empty() ? L"—" : name, rect, selected ? TypeStyle::BodyStrong : TypeStyle::Body, Color::TextPrimary);
        break;
    }
    case 2:
        canvas.drawText(m.record ? m.record->imagePath.wstring() : L"—", rect, TypeStyle::Caption, Color::TextSecondary);
        break;
    case 3: {
        const auto& mounted = m_state.mounted();
        const bool inUse = mounted && _wcsicmp(mounted->mountDir.c_str(), m.folder.c_str()) == 0;
        Str state = Str::SourceMountedOk;
        Color ink = Color::StatusSuccess;
        switch (m.state) {
        case core::MountState::Ok:
            state = inUse ? Str::SourceMountedInUse : m.record && m.record->readOnly ? Str::SourceMountedReadOnly : Str::SourceMountedOk;
            ink = inUse ? Color::AccentBase : Color::StatusSuccess;
            break;
        case core::MountState::NeedsRemount: state = Str::SourceMountedRemount; ink = Color::StatusWarning; break;
        case core::MountState::ImageMissing: state = Str::SourceMountedMissing; ink = Color::StatusError; break;
        case core::MountState::Orphaned: state = Str::SourceMountedOrphan; ink = Color::StatusWarning; break;
        case core::MountState::Foreign: state = Str::SourceMountedForeign; ink = Color::StatusWarning; break;
        default: state = Str::SourceMountedInvalid; ink = Color::StatusError; break;
        }
        canvas.drawText(m_strings.get(state), rect, TypeStyle::Caption, ink);
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
    m_error->setVisible(true);
    layout();
}

void SourcePage::setDragState(ui::DropZone::DragState state) {
    m_drop->setDragState(state);
}

void SourcePage::layout() {
    const RectF b = bounds();
    float y = b.y + kTopGap;
    m_drop->setBounds({b.x, y, b.width, kZoneHeight}); // live-system card removed (D-021): full width
    y += kZoneHeight;
    if (m_error->visible()) {
        y += kGap;
        m_error->setBounds({b.x, y, b.width, kInfoBarHeight});
        y += kInfoBarHeight;
    }
    if (m_host->visible()) {
        y += kGap;
        m_host->setBounds({b.x, y, b.width, kInfoBarHeight});
        y += kInfoBarHeight;
    }
    if (m_mounts->visible()) {
        y += kSectionGap + kSectionLine + kSectionToTable;
        const int rows = std::min(static_cast<int>(m_mountRows.size()), 5);
        const float h = ui::TableView::kHeader + ui::TableView::kRow * static_cast<float>(rows) + 1;
        m_mounts->setBounds({b.x, y, b.width, h});
        y += h + 4 + kSectionLine; // the hint under it
    }
    y += kSectionGap + kSectionLine + kSectionToTable;
    m_recent->setBounds({b.x, y, b.width, m_recent->contentHeight()});
}

void SourcePage::paint(ui::Canvas& canvas) {
    if (m_mounts->visible()) {
        const RectF r = m_mounts->bounds();
        canvas.drawText(m_strings.get(Str::SourceMounted), {r.x, r.y - kSectionToTable - kSectionLine, r.width, kSectionLine},
                        TypeStyle::Section, Color::TextSecondary);
        canvas.drawText(m_strings.get(Str::SourceMountedHint), {r.x, r.bottom() + 4, r.width, kSectionLine}, TypeStyle::Caption,
                        Color::TextTertiary);
    }
    if (m_recent->visible()) {
        const RectF r = m_recent->bounds();
        canvas.drawText(m_strings.get(Str::SourceRecent), {r.x, r.y - kSectionToTable - kSectionLine, r.width, kSectionLine},
                        TypeStyle::Section, Color::TextSecondary);
    }
}

} // namespace wl::app
