#include "app/shell/Shell.h"

#include "app/pages/GalleryPage.h"
#include "ui/widget/Host.h"
#include "ui/widgets/EmptyState.h"

#include <cmath>
#include <string>
#include <utility>

namespace wl::app {

namespace size = ui::tokens::size;

Shell::Shell(const Localization& strings, WindowActions actions)
    : m_strings(strings), m_actions(std::move(actions)) {
    auto s = [&](Str key) { return strings.get(key); };
    m_titleBar = &add<TitleBar>(TitleBar::Labels{s(Str::AppName), s(Str::TitleCmdk), s(Str::KbdCtrl),
                                                 s(Str::TitleMinimize), s(Str::TitleClose)});
    m_nav = &add<NavRail>(NavRail::Labels{s(Str::NavCollapse), s(Str::NavExpand), s(Str::KbdCtrl),
                                          s(Str::TooltipsCollapseNav)},
                          [&](Str key) { return strings.get(key); });
    m_status = &add<StatusBar>(s(Str::StatusNoMount), s(Str::StatusApply));

    m_titleBar->minimizeButton().onInvoke = [this] { m_actions.minimize(); };
    m_titleBar->maximizeButton().onInvoke = [this] { m_actions.toggleMaximize(); };
    m_titleBar->closeButton().onInvoke = [this] { m_actions.close(); };
    m_nav->onSelect = [this](PageId page) { showPage(page); };
    m_nav->onToggleCollapse = [this] {
        m_userCollapsed = !navCollapsed();
        setNavCollapsed(m_userCollapsed, /*animated=*/true);
    };

    showPage(PageId::Source);
}

void Shell::showPage(PageId page) {
    m_page = page;
    const PageInfo& info = pageInfo(page);
    m_nav->setActive(page);

    if (m_pageView) {
        removeChild(m_pageView);
        m_pageView = nullptr;
    }
    if (page == PageId::Gallery) {
        m_pageView = &add<PageView>(L"Widget gallery", L"Faz 1.8 — every widget in every state (dev only).");
        m_pageView->setBody<GalleryPage>();
        m_titleBar->setBreadcrumb(L"Widget gallery");
    } else {
        const std::wstring title = m_strings.get(info.title);
        m_pageView = &add<PageView>(title, info.description ? m_strings.get(*info.description) : std::wstring{});
        // Placeholder until the page's roadmap step is done (docs/ROADMAP.md, Faz 3).
        const std::wstring step(info.roadmapStep.begin(), info.roadmapStep.end());
        m_pageView->setBody<ui::EmptyState>(info.icon, m_strings.get(Str::ShellPendingTitle),
                                            m_strings.format(Str::ShellPendingBody, {{L"id", step}}));
        m_titleBar->setBreadcrumb(title);
    }
    layout();
    invalidate();
}

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
        if (m_actions.toggleTheme) {
            m_actions.toggleTheme();
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
