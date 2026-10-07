#include "app/pages/ImagesPage.h"

#include "app/Format.h"
#include "app/pages/PageBits.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Dropdown.h"

#include <cwctype>
#include <format>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kToolbarTop = 12.0f;
constexpr float kToolbar = 24.0f;
constexpr float kGap = 12.0f;
constexpr float kInfoBar = 32.0f;
constexpr float kSearchWidth = 240.0f;
} // namespace

ImagesPage::ImagesPage(AppState& state, ImageController& controller, const Localization& strings, Language language,
                       std::function<void()> chooseSource)
    : m_state(state), m_controller(controller), m_strings(strings), m_language(language) {
    m_empty = &add<ui::EmptyState>(ui::icons::Icon::LayersEditions, strings.get(Str::ImagesEmptyTitle),
                                   strings.get(Str::ImagesEmptyBody));
    m_empty->setAction(strings.get(Str::ImagesEmptyAction)).onInvoke = std::move(chooseSource);

    m_error = &add<ui::InfoBar>(ui::InfoKind::Error, L"", L"", strings.get(Str::CommonClose));
    m_error->setVisible(false);
    m_error->onClose = [this] {
        m_error->setVisible(false);
        layout();
    };

    m_folder = &add<ui::InfoBar>(ui::InfoKind::Warning, L"", L"", strings.get(Str::CommonClose));
    m_folder->setVisible(false);
    m_folder->onClose = [this] {
        m_folder->setVisible(false);
        layout();
    };

    m_strip = &add<OperationStrip>(strings, state);
    m_strip->setVisible(false);
    m_strip->onCancel = [this] { m_controller.cancel(); };

    m_search = &add<ui::SearchBox>(strings.get(Str::ImagesSearch), std::vector<std::wstring>{L"/"});
    m_search->setAccessible(ui::AccessRole::Edit, strings.get(Str::ImagesSearch));
    m_search->onChange = [this](const std::wstring& text) {
        m_needle = text;
        m_table->setImages(visibleImages());
        m_table->setSelection(m_state.selectedIndex(), m_state.selection());
        layout();
        invalidate();
    };
    m_arch = &add<ui::Dropdown>(strings.get(Str::ImagesArch), archFilterItems(strings), 0);
    m_arch->onChange = [this](int index) {
        m_archFilter = index;
        m_table->setImages(visibleImages());
        m_table->setSelection(m_state.selectedIndex(), m_state.selection());
        layout();
        invalidate();
    };

    m_table = &add<EditionTable>(strings, language);
    m_table->onSelect = [this](int primary, std::vector<int> marked) { m_state.selectMany(std::move(marked), primary); };
    m_table->onActivate = [this](int index) {
        if (m_controller.canMount()) {
            m_controller.mount(index);
        }
    };
    m_table->onDelete = [this] {
        if (onDelete) {
            onDelete();
        }
    };
    m_table->onRename = [this] {
        if (onRename && m_state.selection().size() == 1) {
            onRename();
        }
    };
    m_table->onCopy = [this] {
        if (onCopyInfo) {
            onCopyInfo();
        }
    };
    m_table->onMenu = [this](int, ui::PointF at) { return showRowMenu(at); };

    m_subscription = m_state.subscribe([this](AppState::Change change) { refresh(change); });
    refresh(AppState::Change::Source);
}

ImagesPage::~ImagesPage() {
    m_state.unsubscribe(m_subscription);
}

Str ImagesPage::remedyText(core::Remedy remedy) noexcept {
    switch (remedy) {
    case core::Remedy::CloseOpenFiles: return Str::RemedyCloseOpenFiles;
    case core::Remedy::RepairFolder: return Str::RemedyRepairFolder;
    case core::Remedy::Remount: return Str::RemedyRemount;
    case core::Remedy::UnmountFirst: return Str::RemedyUnmountFirst;
    case core::Remedy::ConvertEsd: return Str::RemedyConvertEsd;
    case core::Remedy::CheckPermissions: return Str::RemedyCheckPermissions;
    case core::Remedy::DisableScanners: return Str::RemedyDisableScanners;
    case core::Remedy::UseLocalFixedDrive: return Str::RemedyUseLocalFixedDrive;
    case core::Remedy::WaitForOther: return Str::RemedyWaitForOther;
    case core::Remedy::FreeDiskSpace: return Str::RemedyFreeDiskSpace;
    case core::Remedy::NotSupported: return Str::RemedyNotSupported;
    case core::Remedy::WimLibrary: return Str::RemedyWimLibrary;
    case core::Remedy::None: break;
    }
    return Str::RemedyUnknown;
}

Str ImagesPage::folderStateText(core::MountState state) noexcept {
    switch (state) {
    case core::MountState::Free: return Str::ImagesFolderFree;
    case core::MountState::Ok: return Str::ImagesFolderOk;
    case core::MountState::NeedsRemount: return Str::ImagesFolderNeedsRemount;
    case core::MountState::Invalid: return Str::ImagesFolderInvalid;
    case core::MountState::ImageMissing: return Str::ImagesFolderImageMissing;
    case core::MountState::Orphaned: return Str::ImagesFolderOrphaned;
    }
    return Str::ImagesFolderInvalid;
}

bool ImagesPage::remedyRepairs(core::Remedy remedy) noexcept {
    return remedy == core::Remedy::CloseOpenFiles || remedy == core::Remedy::RepairFolder ||
           remedy == core::Remedy::Remount;
}

void ImagesPage::updateFolderBar() {
    const auto& folder = m_state.mountFolder();
    // Free = nothing to say; Ok while the app shows it as mounted = normal. Anything else
    // (leftovers, invalid, a mount the app does not know about) gets the bar.
    const bool clean = !folder || folder->state == core::MountState::Free ||
                       (folder->state == core::MountState::Ok && m_state.mounted().has_value());
    if (clean || m_state.operation()) {
        if (m_folder->visible()) {
            m_folder->setVisible(false);
            layout();
        }
        return;
    }
    if (folder->state == core::MountState::Ok && folder->record) {
        // A healthy mount the app does not show (e.g. restore did not run): say what it is and
        // offer to continue with it.
        const auto& r = *folder->record;
        m_folder->set(ui::InfoKind::Info,
                      m_strings.format(Str::ImagesFolderFound,
                                       {{L"edition", folder->imageName.empty() ? L"?" : folder->imageName},
                                        {L"index", std::to_wstring(r.index)}}),
                      r.imagePath.wstring());
        m_folder->setAction(m_strings.get(Str::ImagesFolderContinue), [this] {
            if (onContinueMount) {
                onContinueMount();
            }
        });
        m_folder->setVisible(true);
        layout();
        return;
    }
    std::wstring body = m_strings.get(remedyText(folder->state == core::MountState::NeedsRemount
                                                     ? core::Remedy::Remount
                                                 : folder->state == core::MountState::Ok ? core::Remedy::UnmountFirst
                                                                                         : core::Remedy::RepairFolder));
    if (!folder->blockers.empty()) {
        std::wstring list;
        for (const auto& b : folder->blockers) {
            list += (list.empty() ? L"" : L", ") + b.name +
                    (b.path.empty() ? std::wstring() : L" (" + b.path.wstring() + L")");
        }
        body += L" " + m_strings.format(Str::ImagesFolderBlockers, {{L"list", list}});
    }
    m_folder->set(ui::InfoKind::Warning,
                  m_strings.format(Str::ImagesFolderTitle, {{L"state", m_strings.get(folderStateText(folder->state))}}),
                  body);
    if (folder->state != core::MountState::Ok) {
        m_folder->setAction(m_strings.get(Str::ImagesFolderRepair), [this] {
            m_folder->setVisible(false);
            m_controller.cleanupMounts();
        });
    } else {
        m_folder->setAction(L"", nullptr);
    }
    m_folder->setVisible(true);
    layout();
}

void ImagesPage::showFailure(const std::wstring& title, const std::wstring& message, bool offerRepair) {
    m_error->set(ui::InfoKind::Error, title, message);
    if (offerRepair) {
        m_error->setAction(m_strings.get(Str::ImagesFolderRepair), [this] {
            m_error->setVisible(false);
            m_controller.cleanupMounts();
        });
    } else {
        m_error->setAction(L"", nullptr);
    }
    m_error->setVisible(true);
    layout();
}

bool ImagesPage::showRowMenu(ui::PointF at) {
    const auto index = m_state.selectedIndex();
    if (!host() || !index || m_controller.busy()) {
        return false;
    }
    // Only what can run now: the menu has no disabled items. Copies of the handlers, as picking
    // one may rebuild the page.
    std::vector<std::wstring> labels;
    std::vector<std::function<void()>> actions;
    auto item = [&](Str label, std::function<void()> action) {
        labels.push_back(m_strings.get(label));
        actions.push_back(std::move(action));
    };
    const std::size_t marked = m_state.selection().size();
    const std::size_t editions = m_state.source()->install.images.size();
    const std::wstring count = std::to_wstring(marked);
    auto counted = [&](Str label, std::function<void()> action) {
        labels.push_back(m_strings.format(label, {{L"n", count}}));
        actions.push_back(std::move(action));
    };
    if (marked > 1) {
        // Several editions: what takes them all.
        counted(Str::ImagesExportMany, onExport);
        if (m_controller.canDelete() && marked < editions) {
            counted(Str::ImagesDeleteMany, onDelete);
            counted(Str::ImagesKeepSelected, onKeepOnly);
        }
        item(Str::ImagesCopyInfo, onCopyInfo);
    } else {
        if (m_controller.canMount()) {
            item(Str::ImagesMount, [&controller = m_controller, index = *index] { controller.mount(index); });
        }
        item(Str::ImagesExport, onExport);
        if (const auto& mounted = m_state.mounted(); mounted && mounted->index == *index) {
            item(Str::ImagesUpgrade, onUpgrade);
        }
        if (!m_controller.editRefusal()) {
            item(Str::ImagesRename, onRename);
            item(Str::ImagesDuplicate, onDuplicate);
        }
        if (m_controller.canDelete()) {
            // D-077: Setup lists the editions in file order (an AIO).
            if (*index > 1) {
                item(Str::ImagesMoveUp, [&controller = m_controller, i = *index] { controller.moveEdition(i, -1); });
            }
            if (static_cast<std::size_t>(*index) < editions) {
                item(Str::ImagesMoveDown, [&controller = m_controller, i = *index] { controller.moveEdition(i, +1); });
            }
            item(Str::ImagesDeleteIndex, onDelete);
            if (editions > 2) {
                item(Str::ImagesKeepOnly, onKeepOnly);
            }
        }
        if (const auto& mounted = m_state.mounted(); mounted && mounted->index == *index) {
            item(Str::ImagesExploreMount, onExploreMount);
            item(Str::ImagesTerminalHere, onTerminal);
        }
        item(Str::ImagesRevealFile, onReveal);
        item(Str::ImagesCopyInfo, onCopyInfo);
    }
    auto popup = std::make_unique<ui::MenuPopup>(
        RectF{at.x, at.y, 0, 0}, std::move(labels), -1,
        [actions = std::move(actions)](int picked) {
            if (picked >= 0 && picked < static_cast<int>(actions.size()) && actions[static_cast<std::size_t>(picked)]) {
                actions[static_cast<std::size_t>(picked)]();
            }
        },
        [] {});
    ui::Widget* raw = popup.get();
    host()->pushModal(std::move(popup), raw, /*scrim=*/false);
    return true;
}

std::vector<core::ImageInfo> ImagesPage::visibleImages() const {
    std::vector<core::ImageInfo> out;
    const auto& source = m_state.source();
    if (!source) {
        return out;
    }
    const EditionFilter filter{m_needle, kArchFilterKeys[std::clamp(m_archFilter, 0, 3)]};
    std::ranges::copy_if(source->install.images, std::back_inserter(out), [&](const auto& image) { return filter.matches(image); });
    return out;
}

void ImagesPage::focusSearch() {
    if (host() && m_search->visible()) {
        host()->setFocus(m_search, /*visible=*/true);
    }
}

bool ImagesPage::onChar(wchar_t ch) {
    if (ch == L'/') {
        focusSearch();
        return true;
    }
    return false;
}

void ImagesPage::showNotice(ui::InfoKind kind, const std::wstring& title, const std::wstring& message) {
    m_error->set(kind, title, message);
    m_error->setAction(L"", nullptr);
    m_error->setVisible(true);
    layout();
}

void ImagesPage::refresh(AppState::Change change) {
    const auto& source = m_state.source();
    m_empty->setVisible(!source);
    m_table->setVisible(source.has_value());
    // The search and the filter only help with several editions.
    const bool tools = source && source->install.images.size() > 1;
    m_search->setVisible(tools);
    m_arch->setVisible(tools);
    if (change == AppState::Change::Source) {
        if (!tools) {
            m_needle.clear();
            m_archFilter = 0;
            m_search->setText(L"");
            m_arch->setSelected(0);
        }
        m_table->setImages(visibleImages());
    }
    m_table->setSelection(m_state.selectedIndex(), m_state.selection());
    updateFolderBar();

    const auto& op = m_state.operation();
    const bool wasRunning = m_strip->visible();
    m_strip->setVisible(op.has_value());
    if (op && !wasRunning) {
        m_strip->start();
        m_error->setVisible(false);
    }
    // While its contents are read the image is already mounted: the row says so.
    if (op && op->kind != EngineOperation::Kind::Reading) {
        // "Bağlanıyor" is for a mount; any other work only dims the rows it is not about.
        const bool mounting = op->kind == EngineOperation::Kind::Mounting || op->kind == EngineOperation::Kind::Preparing;
        m_table->setRowState(op->index, mounting ? EditionTable::RowState::Working : EditionTable::RowState::Normal, true);
    } else if (const auto& mounted = m_state.mounted()) {
        m_table->setRowState(mounted->index, EditionTable::RowState::Mounted, false);
    } else if (const auto failed = m_controller.failedIndex()) {
        m_table->setRowState(*failed, EditionTable::RowState::Failed, false);
    } else {
        m_table->setRowState(std::nullopt, EditionTable::RowState::Normal, false);
    }
    layout();
    invalidate();
}

void ImagesPage::layout() {
    const RectF b = bounds();
    m_empty->setBounds(b);
    {
        float x = b.x;
        m_search->setBounds({x, b.y + kToolbarTop, kSearchWidth, kToolbar});
        x += kSearchWidth + 8;
        const float archWidth = std::min(m_arch->measure({}).width, 200.0f);
        m_arch->setBounds({x, b.y + kToolbarTop, archWidth, kToolbar});
    }
    float y = b.y + kToolbarTop + kToolbar + kGap;
    if (m_error->visible()) {
        m_error->setBounds({b.x, y, b.width, kInfoBar});
        y += kInfoBar + kGap;
    }
    if (m_folder->visible()) {
        m_folder->setBounds({b.x, y, b.width, kInfoBar});
        y += kInfoBar + kGap;
    }
    if (m_strip->visible()) {
        m_strip->setBounds({b.x, y, b.width, OperationStrip::kHeight});
        y += OperationStrip::kHeight + kGap;
    }
    m_table->setBounds({b.x, y, b.width, m_table->contentHeight()});
}

void ImagesPage::paint(ui::Canvas& canvas) {
    const auto& source = m_state.source();
    if (!source) {
        return;
    }
    // Toolbar row: summary on the right ("install.wim · 6 index · 6,72 GB").
    const RectF b = bounds();
    const std::wstring summary = m_strings.format(
        Str::ImagesSummary, {{L"file", std::filesystem::path(source->installImage).filename().wstring()},
                             {L"n", std::to_wstring(source->install.images.size())},
                             {L"size", formatBytes(source->installImageSize, m_language)}});
    canvas.drawText(summary, {b.x, b.y + kToolbarTop, b.width, kToolbar}, TypeStyle::Caption, Color::TextSecondary,
                    ui::TextAlign::Trailing);
}

} // namespace wl::app
