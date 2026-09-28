#include "app/shell/Shell.h"

#include "app/pages/GalleryPage.h"
#include "app/pages/SourcePage.h"
#include "base/Log.h"
#include "core/image/Source.h"
#include "core/system/Privileges.h"
#include "ui/platform/FileDialog.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Dialog.h"
#include "ui/widgets/EmptyState.h"

#include <cmath>
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

} // namespace

Shell::Shell(const Localization& strings, Language language, AppState& state, Services services)
    : m_strings(strings), m_language(language), m_state(state), m_services(std::move(services)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_titleBar = &add<TitleBar>(TitleBar::Labels{s(Str::AppName), s(Str::TitleCmdk), s(Str::KbdCtrl),
                                                 s(Str::TitleMinimize), s(Str::TitleClose)});
    m_nav = &add<NavRail>(NavRail::Labels{s(Str::NavCollapse), s(Str::NavExpand), s(Str::KbdCtrl),
                                          s(Str::TooltipsCollapseNav)},
                          [&](Str key) { return strings.get(key); });
    m_status = &add<StatusBar>(s(Str::StatusNoMount), s(Str::StatusApply));

    m_titleBar->minimizeButton().onInvoke = [this] { m_services.minimize(); };
    m_titleBar->maximizeButton().onInvoke = [this] { m_services.toggleMaximize(); };
    m_titleBar->closeButton().onInvoke = [this] { m_services.close(); };
    m_nav->onSelect = [this](PageId page) { showPage(page); };
    m_nav->onToggleCollapse = [this] {
        m_userCollapsed = !navCollapsed();
        setNavCollapsed(m_userCollapsed, /*animated=*/true);
    };
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Source) {
            updateBreadcrumb();
        }
    });
    showPage(PageId::Source);
}

Shell::~Shell() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

SourcePage* Shell::sourcePage() const {
    return m_page == PageId::Source ? static_cast<SourcePage*>(m_pageBody) : nullptr;
}

void Shell::updateBreadcrumb() {
    // With a source: image file › (edition, state — P02). Without: the page title.
    if (const auto& source = m_state.source()) {
        m_titleBar->setBreadcrumb({source->path.filename().empty() ? source->path.wstring()
                                                                    : source->path.filename().wstring()});
    } else if (m_page == PageId::Gallery) {
        m_titleBar->setBreadcrumb({L"Widget gallery"});
    } else {
        m_titleBar->setBreadcrumb({m_strings.get(pageInfo(m_page).title)});
    }
}

void Shell::showPage(PageId page) {
    m_page = page;
    const PageInfo& info = pageInfo(page);
    m_nav->setActive(page);

    if (m_pageView) {
        removeChild(m_pageView);
        m_pageView = nullptr;
        m_pageBody = nullptr;
    }
    if (page == PageId::Gallery) {
        m_pageView = &add<PageView>(L"Widget gallery", L"Faz 1.8 — every widget in every state (dev only).");
        m_pageBody = &m_pageView->setBody<GalleryPage>();
    } else {
        const std::wstring title = m_strings.get(info.title);
        m_pageView = &add<PageView>(title, info.description ? m_strings.get(*info.description) : std::wstring{});
        if (page == PageId::Source) {
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::SourceOpenFile), ui::icons::Icon::OpenFolder)
                .onInvoke = [this] {
                pickSourceFile();
            };
            m_pageView->addAction(ui::ButtonKind::Secondary, m_strings.get(Str::SourceOpenFolder)).onInvoke = [this] {
                pickSourceFolder();
            };
            m_pageBody = &m_pageView->setBody<SourcePage>(
                m_state, m_strings, m_language,
                SourcePage::Intents{[this] { pickSourceFile(); },
                                    [this](const std::filesystem::path& p) { openSource(p); }});
        } else {
            // Placeholder until the page's roadmap step is done (docs/ROADMAP.md, Faz 3).
            const std::wstring step(info.roadmapStep.begin(), info.roadmapStep.end());
            m_pageBody = &m_pageView->setBody<ui::EmptyState>(info.icon, m_strings.get(Str::ShellPendingTitle),
                                                              m_strings.format(Str::ShellPendingBody, {{L"id", step}}));
        }
    }
    updateBreadcrumb();
    layout();
    invalidate();
}

// ---- sources -------------------------------------------------------------------------------

void Shell::openSource(const std::filesystem::path& path) {
    if (m_opening) {
        return;
    }
    m_opening = true;
    if (auto* page = sourcePage()) {
        page->setLoading(true);
    }
    log::info("app", L"opening source " + path.wstring());
    // Engine thread → UI thread: `done` runs on the engine, the page update is posted back.
    m_state.engine().run<core::SourceInfo>(
        [path](const core::TaskContext&) { return core::openSource(path); },
        [this, post = m_services.postToUi, alive = std::weak_ptr<bool>(m_alive)](Result<core::SourceInfo> result) {
            // Engine thread: touch nothing of the shell — only hand the result to the UI thread,
            // where the shell lives and dies; the liveness check happens there.
            post([this, alive, result = std::move(result)]() mutable {
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
                    }
                    return;
                }
                m_state.setSource(std::move(*result));
                showPage(PageId::Images);
            });
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

void Shell::showAdminRequired() {
    if (!host()) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(m_strings.get(Str::DialogsAdminTitle), m_strings.get(Str::DialogsAdminBody),
                                               ui::icons::Icon::ShieldWarning, ui::tokens::Color::StatusWarning, 440.0f);
    ui::Dialog* raw = dialog.get();
    raw->onCancel = [this, raw] { host()->popModal(raw); };
    raw->addButton(ui::ButtonKind::Secondary, m_strings.get(Str::CommonCancel), [this, raw] { host()->popModal(raw); });
    raw->addButton(ui::ButtonKind::Primary, m_strings.get(Str::DialogsAdminGo), [this, raw] {
        host()->popModal(raw);
        if (m_services.relaunchElevated && m_services.relaunchElevated()) {
            m_services.close();
        }
    }, /*primary=*/true);
    // interaction.md: consequential dialogs focus the cancel button first (added first).
    host()->pushModal(std::move(dialog));
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
    if (m_pageView) {
        m_pageView->setBounds({b.x + navWidth, top, std::max(b.width - navWidth, 0.0f), middle});
    }
}

} // namespace wl::app
