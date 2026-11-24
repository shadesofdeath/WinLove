#include "app/ChangelogWindow.h"

#include "base/Log.h"
#include "ui/render/Canvas.h"

#include <chrono>

namespace wl::app {

namespace {

COLORREF colorRef(ui::ThemeKind theme, ui::tokens::Color token) {
    const std::uint32_t argb = ui::colorArgb(theme, token);
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

constexpr ui::SizeF kSize{780, 820};
constexpr ui::SizeF kMinimum{520, 420};

} // namespace

ChangelogWindow::ChangelogWindow(Context context) : m_context(std::move(context)) {}

ChangelogWindow::~ChangelogWindow() {
    m_context.state.unsubscribe(m_subscription);
    m_host.reset(); // the widgets measure with the graphics' text styles: before anything else goes
}

Result<std::unique_ptr<ChangelogWindow>> ChangelogWindow::open(Context context) {
    std::unique_ptr<ChangelogWindow> self(new ChangelogWindow(std::move(context)));
    ChangelogWindow* w = self.get();
    ui::HostServices services{
        [w] { w->m_window.invalidate(); },
        [w](UINT id, UINT ms) { w->m_window.setTimer(id, ms); },
        [w](UINT id) { w->m_window.stopTimer(id); },
    };
    services.text = w->m_context.graphics.text.get();
    services.doubleClickArea = [w] { return w->m_window.doubleClickArea(); };
    w->m_host = std::make_unique<ui::Host>(std::move(services));
    ChangelogView::Services view;
    view.minimize = [w] { w->m_window.minimize(); };
    view.toggleMaximize = [w] { w->m_window.toggleMaximize(); };
    view.close = [w] { w->m_window.close(); };
    view.openLink = w->m_context.openLink;
    view.install = w->m_context.install;
    view.openRelease = w->m_context.openRelease;
    auto root = std::make_unique<ChangelogView>(w->m_context.strings, w->m_context.language, std::move(view));
    w->m_view = root.get();
    w->m_host->setRoot(std::move(root));
    w->refresh();

    ui::WindowCallbacks callbacks;
    callbacks.paint = [w] { w->paint(); };
    callbacks.resized = [w](ui::SizeF size, float scale) {
        if (w->m_target) {
            if (auto r = w->m_target->resize(w->m_window.clientWidthPx(), w->m_window.clientHeightPx(), scale * 96.0f); !r) {
                w->m_failed = true; // drawn again by paint(), which closes the window when it cannot
            }
        }
        w->m_host->layout(size);
        w->paint();
    };
    callbacks.hitTest = [w](ui::PointF p) { return w->m_host->windowZone(p); };
    callbacks.pointer = [w](const ui::PointerEvent& e) { w->m_host->onPointer(e); };
    callbacks.wheel = [w](ui::PointF p, float lines) { w->m_host->onWheel(p, lines); };
    callbacks.character = [w](wchar_t ch) { w->m_host->onChar(ch); };
    callbacks.cursor = [w](ui::PointF p) { return w->m_host->cursorAt(p); };
    callbacks.keyDown = [w](const ui::KeyEvent& key) {
        if (!w->m_host->onKeyDown(key)) {
            w->m_view->onKeyDown(key); // Esc, Ctrl+C / Ctrl+A, scrolling from anywhere
        }
    };
    callbacks.timer = [w](UINT id) { w->m_host->onTimer(id); };
    callbacks.activated = [w](bool active) { w->m_view->setWindowActive(active); };
    callbacks.maximizedChanged = [w](bool maximized) { w->m_view->setMaximized(maximized); };
    callbacks.settingsChanged = [w] { w->m_window.invalidate(); };
    callbacks.closeRequested = [] { return true; };
    callbacks.destroyed = [w] {
        if (auto closed = w->m_context.closed) {
            closed(); // the owner lets this object go later, outside the window procedure
        }
    };
    const ui::ThemeKind theme = w->m_context.theme();
    ui::WindowAppearance appearance{w->m_context.icon, w->m_context.iconResource, theme != ui::ThemeKind::Light,
                                    colorRef(theme, ui::tokens::Color::AccentBase), colorRef(theme, ui::tokens::Color::BgBase)};
    appearance.className = L"WinLove.Changelog";
    appearance.owner = w->m_context.owner;
    appearance.quitOnClose = false;
    if (auto created = w->m_window.create(w->m_context.strings.get(Str::ChangelogWindowTitle).c_str(), kSize, kMinimum,
                                          std::move(callbacks), appearance);
        !created) {
        return std::unexpected(created.error());
    }
    w->m_window.setZoom(w->m_context.zoom);
    auto target = ui::SwapChainTarget::create(*w->m_context.graphics.device, w->m_window.hwnd(), w->m_window.clientWidthPx(),
                                              w->m_window.clientHeightPx(), w->m_window.scale() * 96.0f);
    if (!target) {
        return std::unexpected(target.error());
    }
    w->m_target = std::move(*target);
    w->m_host->layout(w->m_window.clientSize());
    w->m_host->setFocus(&w->m_view->notes(), false);
    w->m_subscription = w->m_context.state.subscribe([w](AppState::Change change) {
        if (change == AppState::Change::AppUpdate) {
            w->refresh();
        }
    });
    w->m_window.show(false);
    log::info("update", L"changelog window opened");
    return self;
}

void ChangelogWindow::refresh() {
    const auto& u = m_context.state.appUpdate();
    if (u.result) {
        m_view->setCheck(*u.result, nowSeconds());
    }
    const bool newer = u.result && u.result->verdict == core::ReleaseVerdict::Available;
    const bool busy = u.install && u.install->stage != AppState::AppUpdate::Install::Stage::Failed;
    m_view->setInstallable(newer && !busy);
    m_window.invalidate();
}

void ChangelogWindow::applyTheme() {
    const ui::ThemeKind theme = m_context.theme();
    m_window.setFrameColors(theme != ui::ThemeKind::Light, colorRef(theme, ui::tokens::Color::AccentBase));
    m_window.invalidate();
}

void ChangelogWindow::setZoom(float zoom) {
    m_context.zoom = zoom;
    m_window.setZoom(zoom);
}

void ChangelogWindow::paint() {
    if (!m_target || !m_host) {
        return;
    }
    Result<void> presented = [this] {
        ui::Canvas canvas(m_target->beginDraw(), m_context.theme(), m_window.scale(), *m_context.graphics.text,
                          *m_context.graphics.icons);
        canvas.clear(ui::tokens::Color::BgBase);
        m_host->paint(canvas);
        return m_target->endDrawAndPresent();
    }();
    if (!presented || m_failed) {
        // A lost device (the main window makes a new one) or a failed resize: this window goes; it
        // opens again from the same button.
        log::warn("update", L"the changelog window could not be drawn and was closed" +
                                (presented ? std::wstring() : L": " + describe(presented.error())));
        m_target.reset();
        m_window.close();
    }
}

} // namespace wl::app
