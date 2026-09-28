#pragma once
// Root widget of the main window (layout-system.md): TitleBar 32 on top, StatusBar 24 at the
// bottom, NavRail 200/44 on the left, the current page in the rest. Owns page switching,
// nav collapse animation and app-level shortcuts (interaction.md "Kısayollar").
#include "app/Localization.h"
#include "app/pages/PageInfo.h"
#include "app/shell/NavRail.h"
#include "app/shell/PageView.h"
#include "app/shell/StatusBar.h"
#include "app/shell/TitleBar.h"
#include "ui/anim/Tween.h"

#include <functional>

namespace wl::app {

class Shell : public ui::Widget {
public:
    struct WindowActions {
        std::function<void()> minimize;
        std::function<void()> toggleMaximize;
        std::function<void()> close;
        std::function<void()> toggleTheme;
    };

    Shell(const Localization& strings, WindowActions actions);

    TitleBar& titleBar() { return *m_titleBar; }
    NavRail& nav() { return *m_nav; }
    [[nodiscard]] PageId currentPage() const noexcept { return m_page; }

    void showPage(PageId page);
    void setNavCollapsed(bool collapsed, bool animated);
    [[nodiscard]] bool navCollapsed() const noexcept { return m_navTarget < 0.5f; }
    // Shortcuts not consumed by the focused widget. Returns true if handled.
    bool handleShortcut(const ui::KeyEvent& key);

    void layout() override;
    bool tick(double now) override;

private:
    const Localization& m_strings;
    WindowActions m_actions;
    TitleBar* m_titleBar = nullptr;
    NavRail* m_nav = nullptr;
    StatusBar* m_status = nullptr;
    PageView* m_pageView = nullptr;
    PageId m_page = PageId::Source;
    ui::Tween m_navExpansion{1.0f};
    float m_navTarget = 1.0f;
    bool m_userCollapsed = false; // the user's choice; narrow windows collapse on top of it
    bool m_narrow = false;        // width < 1200 (layout-system.md breakpoint)
};

} // namespace wl::app
