#pragma once
// D-145: the changelog's own top-level window — the same framework as the main window (custom title
// bar, the theme and accent, Per-Monitor DPI, the user's zoom), owned by it (above it, minimised with
// it), resizable, Esc closes it, at most one (App keeps it and brings it to the front). Its content is
// app/shell/Changelog's ChangelogView; it follows AppState's update state while it is open.
#include "app/Localization.h"
#include "app/shell/Changelog.h"
#include "app/state/AppState.h"
#include "ui/platform/Window.h"
#include "ui/render/Graphics.h"
#include "ui/render/SwapChainTarget.h"
#include "ui/widget/Host.h"

#include <functional>
#include <memory>

namespace wl::app {

class ChangelogWindow {
public:
    struct Context {
        ui::Graphics& graphics;
        AppState& state;
        const Localization& strings;
        Language language;
        std::function<ui::ThemeKind()> theme;
        float zoom = 1.0f;
        HWND owner = nullptr;
        HICON icon = nullptr;
        const wchar_t* iconResource = nullptr;
        std::function<void(const std::wstring& url)> openLink;
        std::function<void()> install;
        std::function<void()> openRelease;
        std::function<void()> closed; // the window is gone: the owner drops this object (later, not inside it)
    };

    [[nodiscard]] static Result<std::unique_ptr<ChangelogWindow>> open(Context context);
    ~ChangelogWindow();
    ChangelogWindow(const ChangelogWindow&) = delete;
    ChangelogWindow& operator=(const ChangelogWindow&) = delete;

    void bringToFront() { m_window.bringToFront(); }
    void close() { m_window.close(); }
    void applyTheme();     // the theme or the accent changed
    void setZoom(float zoom);

private:
    explicit ChangelogWindow(Context context);
    void paint();
    void refresh(); // the update state: the notes, "Yükle"

    Context m_context;
    ui::Window m_window;
    std::unique_ptr<ui::SwapChainTarget> m_target;
    std::unique_ptr<ui::Host> m_host;
    ChangelogView* m_view = nullptr;
    std::size_t m_subscription = 0;
    bool m_failed = false;
};

} // namespace wl::app
