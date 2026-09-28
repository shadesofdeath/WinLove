#include "app/shell/Shell.h"

#include "app/Format.h"
#include "app/pages/GalleryPage.h"
#include "app/pages/ImagesPage.h"
#include "app/pages/LogsPage.h"
#include "app/pages/SourcePage.h"
#include "app/pages/images/ImageInspector.h"
#include "base/Log.h"
#include "base/Path.h"
#include "base/Utf8.h"
#include "core/image/Source.h"
#include "core/image/dism/DismErrors.h"
#include "ui/platform/FileDialog.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Dialog.h"
#include "ui/widgets/EmptyState.h"

#include <cmath>
#include <format>
#include <fstream>
#include <string>
#include <utility>

namespace wl::app {

namespace size = ui::tokens::size;

namespace {

bool isSourceCandidate(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return true; // extracted setup folder; validated when opened
    }
    switch (core::formatFromPath(path.wstring())) {
    case core::ImageFormat::Iso:
    case core::ImageFormat::Wim:
    case core::ImageFormat::Esd:
    case core::ImageFormat::Swm: return true;
    default: return false;
    }
}

constexpr float kToastMargin = 16.0f;

} // namespace

Shell::Shell(const Localization& strings, Language language, AppState& state, Services services)
    : m_strings(strings), m_language(language), m_state(state), m_services(std::move(services)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_titleBar = &add<TitleBar>(TitleBar::Labels{s(Str::AppName), s(Str::TitleCmdk), s(Str::KbdCtrl),
                                                 s(Str::TitleMinimize), s(Str::TitleClose)});
    m_nav = &add<NavRail>(NavRail::Labels{s(Str::NavCollapse), s(Str::NavExpand), s(Str::KbdCtrl),
                                          s(Str::TooltipsCollapseNav)},
                          [&](Str key) { return strings.get(key); });
    m_status = &add<StatusBar>(StatusBar::Labels{s(Str::StatusNoMount), s(Str::StatusMounted), s(Str::StatusImage),
                                                 s(Str::StatusApply)});

    m_titleBar->minimizeButton().onInvoke = [this] { m_services.minimize(); };
    m_titleBar->maximizeButton().onInvoke = [this] { m_services.toggleMaximize(); };
    m_titleBar->closeButton().onInvoke = [this] { m_services.close(); };
    m_nav->onSelect = [this](PageId page) { showPage(page); };
    m_nav->onToggleCollapse = [this] {
        m_userCollapsed = !navCollapsed();
        setNavCollapsed(m_userCollapsed, /*animated=*/true);
    };

    m_images = std::make_unique<ImageController>(m_state, ImageController::Events{
        m_services.postToUi,
        [this](ImageController::Failure f, const Error& e, int index) { onImageFailure(f, e, index); },
        [this](Str title, std::wstring detail) {
            showToast(ui::InfoKind::Success, m_strings.format(title, {{L"edition", detail}, {L"file", detail}}), L"");
        },
        [this](Str title) { showToast(ui::InfoKind::Warning, m_strings.get(title), L""); },
        [this](std::wstring args) { showAdminRequired(std::move(args)); },
        [this](std::filesystem::path source, MountedImage mounted) { restoreMount(source, std::move(mounted)); },
    });

    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::MountFolder && !m_autoRestoreTried) {
            const auto& folder = m_state.mountFolder();
            if (folder && folder->state == core::MountState::Ok && folder->record && !m_state.mounted()) {
                m_autoRestoreTried = true;
                continueFolderMount();
            }
        }
        if (change != AppState::Change::Recent) {
            updateBreadcrumb();
            updateImagesChrome();
            updateStatus();
        }
    });
    showPage(PageId::Source);
    updateStatus();
}

Shell::~Shell() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

SourcePage* Shell::sourcePage() const {
    return m_page == PageId::Source ? static_cast<SourcePage*>(m_pageBody) : nullptr;
}

ImagesPage* Shell::imagesPage() const {
    return m_page == PageId::Images ? static_cast<ImagesPage*>(m_pageBody) : nullptr;
}

LogsPage* Shell::logsPage() const {
    return m_page == PageId::Logs ? dynamic_cast<LogsPage*>(m_pageBody) : nullptr;
}

bool Shell::inspectorVisible() const {
    // Screen 02 shows it for the selected edition; screen 03 hides it while the engine works.
    return m_inspector && m_page == PageId::Images && m_state.selectedImage() && !m_state.operation();
}

void Shell::updateBreadcrumb() {
    // statusbar-titlebar.md: image › edition › state. Without a source: the page title.
    if (const auto& source = m_state.source()) {
        std::vector<std::wstring> parts{source->path.filename().empty() ? source->path.wstring()
                                                                         : source->path.filename().wstring()};
        if (const auto& mounted = m_state.mounted()) {
            parts.push_back(mounted->edition);
            parts.push_back(m_strings.get(Str::StatusMounted));
        } else if (const auto& op = m_state.operation(); op && op->kind == EngineOperation::Kind::Mounting) {
            parts.push_back(op->edition);
            parts.push_back(m_strings.get(Str::StatusMounting));
        } else if (const auto* image = m_state.selectedImage(); image && m_page == PageId::Images) {
            parts.push_back(image->name);
        }
        m_titleBar->setBreadcrumb(std::move(parts));
    } else if (m_page == PageId::Gallery) {
        m_titleBar->setBreadcrumb({L"Widget gallery"});
    } else {
        m_titleBar->setBreadcrumb({m_strings.get(pageInfo(m_page).title)});
    }
}

void Shell::updateStatus() {
    if (const auto& mounted = m_state.mounted()) {
        std::wstring imageSize;
        if (const auto& source = m_state.source()) {
            for (const auto& image : source->install.images) {
                if (image.index == mounted->index) {
                    imageSize = formatBytes(image.totalBytes, m_language);
                }
            }
        }
        m_status->setMount(mounted->mountDir.wstring(), imageSize);
    } else {
        m_status->setMount(std::nullopt, L"");
    }
    if (const auto& op = m_state.operation()) {
        const Str label = op->kind == EngineOperation::Kind::Mounting ? Str::StatusMounting : Str::StatusScanning;
        m_status->setTask(m_strings.get(label), static_cast<float>(op->fraction));
    } else {
        m_status->setTask(std::nullopt, 0);
    }
}

void Shell::updateImagesChrome() {
    const auto* image = m_state.selectedImage();
    const bool mounted = m_state.mounted().has_value();
    const bool mountedHere = mounted && image && m_state.mounted()->index == image->index;
    const bool busy = m_images->busy();
    if (m_actionMount) {
        m_actionMount->setText(m_strings.get(mounted ? Str::ImagesUnmount : Str::ImagesMount));
        m_actionMount->setEnabled(!busy && (mounted || (image && m_images->canMount())));
        m_actionMount->setTooltip(m_images->isEsdSource() ? m_strings.get(Str::ImagesEsdNoMount) : std::wstring{});
    }
    if (m_actionExport) {
        m_actionExport->setEnabled(!busy && image != nullptr);
    }
    if (m_actionEsd) {
        m_actionEsd->setEnabled(!busy && m_images->isEsdSource());
        m_actionEsd->setTooltip(m_images->isEsdSource() ? std::wstring{} : m_strings.get(Str::ImagesEsdOnly));
    }
    if (m_inspector) {
        m_inspector->set(m_state.source() ? &*m_state.source() : nullptr, image, mountedHere,
                         image && m_images->canMount(), image && m_images->canDelete(),
                         m_images->isEsdSource() ? m_strings.get(Str::ImagesEsdNoMount) : std::wstring{},
                         m_images->canDelete() ? std::wstring{} : m_strings.get(Str::ImagesReadOnlySource));
        m_inspector->setVisible(inspectorVisible());
    }
    layout();
    invalidate();
}

void Shell::showPage(PageId page) {
    if (m_page == PageId::Logs && page != PageId::Logs && m_services.stopTimer) {
        m_services.stopTimer(kLogTimer);
    }
    m_page = page;
    const PageInfo& info = pageInfo(page);
    m_nav->setActive(page);

    if (m_pageView) {
        removeChild(m_pageView);
        m_pageView = nullptr;
        m_pageBody = nullptr;
    }
    if (m_inspector) {
        removeChild(m_inspector);
        m_inspector = nullptr;
    }
    m_actionMount = m_actionExport = m_actionEsd = nullptr;

    if (page == PageId::Gallery) {
        m_pageView = &add<PageView>(L"Widget gallery", L"Faz 1.8 — every widget in every state (dev only).");
        m_pageBody = &m_pageView->setBody<GalleryPage>();
    } else {
        const std::wstring title = m_strings.get(info.title);
        m_pageView = &add<PageView>(title, info.description ? m_strings.get(*info.description) : std::wstring{});
        if (page == PageId::Source) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::SourceOpenFile), ui::icons::Icon::OpenFolder)
                .onInvoke = [this] { pickSourceFile(); };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::SourceOpenFolder)).onInvoke = [this] {
                pickSourceFolder();
            };
            m_pageBody = &m_pageView->setBody<SourcePage>(
                m_state, m_strings, m_language,
                SourcePage::Intents{[this] { pickSourceFile(); },
                                    [this](const std::filesystem::path& p) { openSource(p); }});
        } else if (page == PageId::Images) {
            if (m_state.source()) {
                m_actionEsd = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesEsdToWim));
                m_actionEsd->onInvoke = [this] { convertEsd(); };
                m_actionExport = &m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesExport));
                m_actionExport->onInvoke = [this] { exportSelected(); };
                m_actionMount = &m_pageView->addAction(ui::ButtonKind::Primary, m_strings.get(Str::ImagesMount));
                m_actionMount->onInvoke = [this] {
                    if (m_state.mounted()) {
                        askUnmount();
                    } else if (const auto index = m_state.selectedIndex()) {
                        m_images->mount(*index);
                    }
                };
            }
            m_pageBody = &m_pageView->setBody<ImagesPage>(m_state, *m_images, m_strings, m_language,
                                                          [this] { showPage(PageId::Source); });
            static_cast<ImagesPage*>(m_pageBody)->onContinueMount = [this] { continueFolderMount(); };
            m_inspector = &add<ImageInspector>(m_strings, m_language);
            m_inspector->onMount = [this] {
                if (const auto index = m_state.selectedIndex()) {
                    m_images->mount(*index);
                }
            };
            m_inspector->onUnmount = [this] { askUnmount(); };
            m_inspector->onDelete = [this] { askDeleteSelected(); };
        } else if (page == PageId::Logs) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::LogsClear)).onInvoke = [this] {
                if (auto* logs = logsPage()) {
                    logs->clear();
                }
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::CommonExport), ui::icons::Icon::Export)
                .onInvoke = [this] { exportLog(); };
            m_pageBody = &m_pageView->setBody<LogsPage>(m_state, m_strings, m_language);
            if (m_services.startTimer) {
                m_services.startTimer(kLogTimer, 250);
            }
        } else {
            // Placeholder until the page's roadmap step is done (docs/ROADMAP.md, Faz 3).
            const std::wstring step(info.roadmapStep.begin(), info.roadmapStep.end());
            m_pageBody = &m_pageView->setBody<ui::EmptyState>(info.icon, m_strings.get(Str::ShellPendingTitle),
                                                              m_strings.format(Str::ShellPendingBody, {{L"id", step}}));
        }
    }
    // The toast stays on top of whatever page is shown.
    if (!m_toast) {
        m_toast = &add<ui::Toast>();
    }
    bringToFront(m_toast);
    updateBreadcrumb();
    updateImagesChrome();
    layout();
    invalidate();
}

// ---- toast ---------------------------------------------------------------------------------

void Shell::showToast(ui::InfoKind kind, std::wstring title, std::wstring message) {
    m_toast->show(kind, std::move(title), std::move(message));
    layout();
    if (m_services.startTimer) {
        m_services.startTimer(kToastTimer, ui::Toast::kDurationMs);
    }
}

void Shell::onTimer(UINT id) {
    if (id == kLogTimer) {
        if (auto* logs = logsPage()) {
            logs->poll();
        }
        return;
    }
    if (id != kToastTimer || m_toast->hoveredNow()) {
        return; // hovered: keep it; the timer fires again
    }
    if (m_services.stopTimer) {
        m_services.stopTimer(kToastTimer);
    }
    m_toast->hide();
}

// ---- sources -------------------------------------------------------------------------------

void Shell::openSource(const std::filesystem::path& path, std::function<void()> then) {
    if (m_opening) {
        return;
    }
    if (m_images->busy()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesBusy), L"");
        return;
    }
    if (m_state.mounted()) {
        showToast(ui::InfoKind::Warning, m_strings.get(Str::ImagesUnmountFirst), L"");
        return;
    }
    m_opening = true;
    if (auto* page = sourcePage()) {
        page->setLoading(true);
    }
    log::info("app", L"opening source " + path.wstring());
    // Engine thread → UI thread: `done` runs on the engine and only hands over the result.
    m_state.engine().run<core::SourceInfo>(
        [path](const core::TaskContext&) { return core::openSource(path); },
        [this, post = m_services.postToUi, alive = std::weak_ptr<bool>(m_alive),
         then = std::move(then)](Result<core::SourceInfo> result) {
            post([this, alive, then, result = std::move(result)]() mutable {
                const auto stillAlive = alive.lock();
                if (!stillAlive || !*stillAlive) {
                    return;
                }
                m_opening = false;
                if (auto* page = sourcePage()) {
                    page->setLoading(false);
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    if (auto* page = sourcePage()) {
                        page->showError(result.error().message + L" — " + result.error().context);
                    } else {
                        showToast(ui::InfoKind::Error, m_strings.get(Str::SourceOpenFailed), result.error().message);
                    }
                    return;
                }
                m_state.setSource(std::move(*result));
                showPage(PageId::Images);
                if (then) {
                    then();
                }
            });
        });
}

void Shell::continueFolderMount() {
    const auto& folder = m_state.mountFolder();
    if (!folder || !folder->record || m_state.mounted()) {
        return;
    }
    const auto& m = *folder->record;
    restoreMount(sourceForMountedImage(m.imagePath), MountedImage{m.mountPath, m.imagePath, m.index, {}, m.readOnly});
}

void Shell::restoreMount(const std::filesystem::path& source, MountedImage mounted) {
    log::info("app", L"restore: opening " + source.wstring());
    if (m_state.source() && _wcsicmp(m_state.source()->path.c_str(), nativePath(source).c_str()) == 0) {
        // The right source is already open: just mark the mount.
        m_state.select(mounted.index);
        mounted.edition = m_state.selectedImage() ? m_state.selectedImage()->name : std::format(L"#{}", mounted.index);
        const std::wstring edition = mounted.edition;
        m_state.setMounted(std::move(mounted));
        showToast(ui::InfoKind::Info, m_strings.format(Str::ImagesMountRestored, {{L"edition", edition}}), L"");
        return;
    }
    openSource(source, [this, mounted = std::move(mounted)]() mutable {
        mounted.edition = std::format(L"#{}", mounted.index);
        if (const auto& info = m_state.source()) {
            for (const auto& image : info->install.images) {
                if (image.index == mounted.index) {
                    mounted.edition = image.name;
                }
            }
        }
        m_state.select(mounted.index);
        const std::wstring edition = mounted.edition;
        m_state.setMounted(std::move(mounted));
        showToast(ui::InfoKind::Info, m_strings.format(Str::ImagesMountRestored, {{L"edition", edition}}),
                  m_state.mounted()->imagePath.wstring());
    });
}

void Shell::pickSourceFile() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto file = ui::pickFile(owner, m_strings.get(Str::SourceOpenFile),
                                   {{m_strings.get(Str::SourceFilterImages), L"*.iso;*.wim;*.esd;*.swm"},
                                    {m_strings.get(Str::SourceFilterAll), L"*.*"}});
    if (file) {
        openSource(*file);
    }
}

void Shell::pickSourceFolder() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    if (const auto folder = ui::pickFolder(owner, m_strings.get(Str::SourcePickFolder))) {
        openSource(*folder);
    }
}

ui::Dialog& Shell::pushDialog(std::unique_ptr<ui::Dialog> dialog) {
    // Push after the buttons exist: the modal focuses its first focusable (the cancel button).
    ui::Dialog* raw = dialog.get();
    raw->onCancel = [this, raw] { host()->popModal(raw); };
    host()->pushModal(std::move(dialog));
    return *raw;
}

void Shell::showAdminRequired(std::wstring relaunchArgs) {
    if (!host()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::DialogsAdminTitle),
                                                           m_strings.get(Str::DialogsAdminBody), ui::icons::Icon::ShieldWarning,
                                                           ui::tokens::Color::StatusWarning, 440.0f);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::DialogsAdminGo),
                   [this, raw, args = std::move(relaunchArgs)] {
                       host()->popModal(raw);
                       if (m_services.relaunchElevated && m_services.relaunchElevated(args)) {
                           m_services.close();
                       }
                   },
                   /*primary=*/true);
    pushDialog(std::move(dialog));
}

bool Shell::dragEnter(const std::vector<std::filesystem::path>& files) {
    const bool valid = !files.empty() && isSourceCandidate(files.front());
    if (auto* page = sourcePage()) {
        page->setDragState(valid ? ui::DropZone::DragState::Valid : ui::DropZone::DragState::Invalid);
    }
    return valid;
}

void Shell::dragLeave() {
    if (auto* page = sourcePage()) {
        page->setDragState(ui::DropZone::DragState::None);
    }
}

void Shell::drop(const std::vector<std::filesystem::path>& files) {
    // Several files may be dropped; the first source-type one opens (others are ignored for now).
    for (const auto& file : files) {
        if (isSourceCandidate(file)) {
            if (m_page != PageId::Source) {
                showPage(PageId::Source);
            }
            openSource(file);
            return;
        }
    }
}

// ---- images (P02) ----------------------------------------------------------------------------

void Shell::onImageFailure(ImageController::Failure failure, const Error& error, int /*index*/) {
    const std::wstring code = std::format(L"0x{:08X}", static_cast<unsigned>(error.hresult));
    const bool mountFailure = failure == ImageController::Failure::Mount;
    const std::wstring title = mountFailure ? m_strings.format(Str::ImagesMountFailed, {{L"code", code}})
                                            : std::format(L"{} ({})", m_strings.get(Str::ImagesFailedTitle), code);
    // What the code means (DismErrors catalog) decides the advice and whether "Onar" helps; the
    // raw DISM/WIM text goes to the log.
    const auto info = core::explainError(error.hresult);
    log::error("app", std::format(L"{} — {} [{}] {}", code, info.label, core::remedyName(info.remedy),
                                  core::systemMessage(error.hresult)));
    std::wstring message = m_strings.get(ImagesPage::remedyText(info.remedy));
    if (!info.known && !error.message.empty()) {
        message = error.message;
    }
    if (auto* page = imagesPage(); page && m_state.source()) {
        page->showFailure(title, message, ImagesPage::remedyRepairs(info.remedy));
    }
    showToast(ui::InfoKind::Error, mountFailure ? m_strings.get(Str::ToastsMountFailed) : m_strings.get(Str::ImagesFailedTitle),
              m_strings.format(Str::ToastsMountFailedBody, {{L"code", code}}));
}

void Shell::askUnmount() {
    if (!host() || !m_state.mounted()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::ImagesUnmountTitle),
                                                           m_strings.get(Str::ImagesUnmountBody), ui::icons::Icon::Unmount,
                                                           ui::tokens::Color::TextSecondary);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::ImagesUnmountDiscard), [this, raw] {
        host()->popModal(raw);
        m_images->unmount(/*commit=*/false);
    });
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::ImagesUnmountCommit),
                   [this, raw] {
                       host()->popModal(raw);
                       m_images->unmount(/*commit=*/true);
                   },
                   /*primary=*/true);
    pushDialog(std::move(dialog));
}

void Shell::askDeleteSelected() {
    const auto* image = m_state.selectedImage();
    if (!host() || !image) {
        return;
    }
    const int index = image->index;
    auto dialog = std::make_unique<ui::Dialog>(
        m_strings.get(Str::DialogsDeleteIndexTitle),
        m_strings.format(Str::DialogsDeleteIndexBody, {{L"name", std::format(L"{} · {}", index, image->name)}}),
        ui::icons::Icon::ErrorOctagon, ui::tokens::Color::StatusError);
    ui::Dialog* raw = dialog.get();
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    raw->addButton(ui::ButtonKind::Danger, m_strings.get(Str::CommonDelete), [this, raw, index] {
        host()->popModal(raw);
        m_images->deleteIndex(index);
    });
    pushDialog(std::move(dialog));
}

void Shell::exportSelected() {
    const auto* image = m_state.selectedImage();
    if (!image) {
        return;
    }
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::ImagesExport), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
                                         image->name + L".wim", L"wim");
    if (target) {
        m_images->exportIndex(image->index, *target);
    }
}

void Shell::exportLog() {
    auto* logs = logsPage();
    if (!logs) {
        return;
    }
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::LogsExportTitle),
                                         {{m_strings.get(Str::LogsLogFiles), L"*.log;*.txt"}}, L"WinLove.log", L"log");
    if (!target) {
        return;
    }
    std::ofstream out(*target, std::ios::binary | std::ios::trunc);
    const std::string utf8 = utf8::fromWide(logs->exportText());
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    if (out) {
        showToast(ui::InfoKind::Success, m_strings.get(Str::LogsExported), target->wstring());
    } else {
        showToast(ui::InfoKind::Error, m_strings.get(Str::LogsExportFailed), target->wstring());
    }
}

void Shell::convertEsd() {
    const HWND owner = m_services.ownerWindow ? m_services.ownerWindow() : nullptr;
    const auto target = ui::pickSaveFile(owner, m_strings.get(Str::ImagesEsdToWim), {{m_strings.get(Str::ImagesSaveWim), L"*.wim"}},
                                         L"install.wim", L"wim");
    if (target) {
        m_images->convertEsd(*target);
    }
}

// ---- layout, shortcuts -----------------------------------------------------------------------

void Shell::setNavCollapsed(bool collapsed, bool animated) {
    if (!animated) {
        m_userCollapsed = collapsed;
    }
    m_navTarget = collapsed ? 0.0f : 1.0f;
    if (animated && m_navExpansion.animateTo(m_navTarget, ui::tokens::motion::slowMs)) {
        animate();
    } else {
        m_navExpansion.snapTo(m_navTarget);
        m_nav->setExpansion(m_navTarget);
        layout();
    }
    invalidate();
}

bool Shell::tick(double now) {
    const bool running = m_navExpansion.tick(now);
    m_nav->setExpansion(m_navExpansion.value());
    layout();
    invalidate();
    return running;
}

bool Shell::handleShortcut(const ui::KeyEvent& key) {
    if (key.ctrl && !key.shift && !key.alt && key.virtualKey == 'F') {
        if (auto* logs = logsPage()) {
            logs->focusSearch();
            return true;
        }
    }
    if (key.ctrl && !key.shift && !key.alt && key.virtualKey == 'B') {
        m_userCollapsed = !navCollapsed();
        setNavCollapsed(m_userCollapsed, /*animated=*/true);
        return true;
    }
    if (key.ctrl && key.shift && key.virtualKey == 'T') {
        if (m_services.toggleTheme) {
            m_services.toggleTheme();
        }
        return true;
    }
    if (key.ctrl && key.shift && key.virtualKey == 'G') {
        showPage(PageId::Gallery);
        return true;
    }
    if (key.ctrl && !key.shift && key.virtualKey == VK_OEM_COMMA) {
        showPage(PageId::Settings);
        return true;
    }
    if (key.ctrl && !key.shift && !key.alt && key.virtualKey >= '1' && key.virtualKey <= '9') {
        // Ctrl+1…9: nav items in display order.
        int index = static_cast<int>(key.virtualKey - '1');
        for (const auto& page : allPages()) {
            if (page.navGroup >= 0 && index-- == 0) {
                showPage(page.id);
                return true;
            }
        }
    }
    return false;
}

void Shell::layout() {
    const ui::RectF b = bounds();
    // layout-system.md: below 1200 the rail collapses to 44; the user can still open it (Ctrl B),
    // and widening the window restores their own preference.
    constexpr float kNarrowWidth = 1200.0f;
    const bool narrow = b.width > 0 && b.width < kNarrowWidth;
    if (narrow != m_narrow) {
        m_narrow = narrow;
        const float target = (narrow || m_userCollapsed) ? 0.0f : 1.0f;
        m_navTarget = target;
        m_navExpansion.snapTo(target);
        m_nav->setExpansion(target);
    }
    const float navWidth =
        std::round(size::navCollapsed + (size::navExpanded - size::navCollapsed) * m_navExpansion.value());
    m_titleBar->setBounds({b.x, b.y, b.width, size::titleBar});
    m_status->setBounds({b.x, b.bottom() - size::statusBar, b.width, size::statusBar});
    const float top = b.y + size::titleBar;
    const float middle = std::max(b.bottom() - size::statusBar - top, 0.0f);
    m_nav->setBounds({b.x, top, navWidth, middle});
    const float inspectorWidth = inspectorVisible() ? size::inspector : 0.0f;
    if (m_pageView) {
        m_pageView->setBounds({b.x + navWidth, top, std::max(b.width - navWidth - inspectorWidth, 0.0f), middle});
    }
    if (m_inspector) {
        m_inspector->setBounds({b.right() - size::inspector, top, size::inspector, middle});
    }
    if (m_toast) {
        m_toast->setBounds({b.right() - kToastMargin - ui::Toast::kWidth,
                            b.bottom() - size::statusBar - kToastMargin - ui::Toast::kHeight, ui::Toast::kWidth,
                            ui::Toast::kHeight});
    }
}

} // namespace wl::app
