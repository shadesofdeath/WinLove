#include "ui/platform/Window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace wl::ui {

namespace {

constexpr wchar_t kClassName[] = L"WinLove.Window";
constexpr UINT kPostMessage = WM_APP + 1;
std::atomic<int> g_postHolds{0}; // PostHold objects alive (UI thread; atomic for the reader in post paths)

bool isCaptionButton(WPARAM hit) {
    return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE;
}

HitZone zoneFromHit(WPARAM hit) {
    switch (hit) {
    case HTMINBUTTON: return HitZone::MinimizeButton;
    case HTMAXBUTTON: return HitZone::MaximizeButton;
    case HTCLOSE: return HitZone::CloseButton;
    case HTCAPTION: return HitZone::Caption;
    default: return HitZone::Client;
    }
}

LRESULT hitFromZone(HitZone zone) {
    switch (zone) {
    case HitZone::Caption: return HTCAPTION;
    case HitZone::MinimizeButton: return HTMINBUTTON;
    case HitZone::MaximizeButton: return HTMAXBUTTON;
    case HitZone::CloseButton: return HTCLOSE;
    case HitZone::Client: break;
    }
    return HTCLIENT;
}

} // namespace

Window::~Window() {
    releaseTaskbar();
    if (m_hwnd) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
    }
    for (HICON icon : {m_smallIcon, m_bigIcon}) {
        if (icon) {
            DestroyIcon(icon);
        }
    }
}

void Window::setIcons(UINT dpi) {
    if (!m_iconResource || !m_hwnd) {
        return;
    }
    // LoadImageW picks the .ico's closest image at the asked size (user32; LoadIconWithScaleDown
    // would tie the exe to comctl32 v6).
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    auto load = [&](int metricX, int metricY) {
        return static_cast<HICON>(LoadImageW(instance, m_iconResource, IMAGE_ICON, GetSystemMetricsForDpi(metricX, dpi),
                                             GetSystemMetricsForDpi(metricY, dpi), LR_DEFAULTCOLOR));
    };
    HICON smallIcon = load(SM_CXSMICON, SM_CYSMICON);
    HICON bigIcon = load(SM_CXICON, SM_CYICON);
    if (smallIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        if (m_smallIcon) {
            DestroyIcon(m_smallIcon);
        }
        m_smallIcon = smallIcon;
    }
    if (bigIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
        if (m_bigIcon) {
            DestroyIcon(m_bigIcon);
        }
        m_bigIcon = bigIcon;
    }
}

SizeF Window::doubleClickArea() const {
    const UINT dpi = m_hwnd ? GetDpiForWindow(m_hwnd) : 96;
    return {static_cast<float>(GetSystemMetricsForDpi(SM_CXDOUBLECLK, dpi)) / m_scale,
            static_cast<float>(GetSystemMetricsForDpi(SM_CYDOUBLECLK, dpi)) / m_scale};
}

Result<void> Window::create(const wchar_t* title, SizeF initialSize, SizeF minimumSize, WindowCallbacks callbacks,
                            const WindowAppearance& appearance) {
    m_callbacks = std::move(callbacks);
    m_minimum = minimumSize;
    m_iconResource = appearance.iconResource;
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    m_quitOnClose = appearance.quitOnClose;
    const wchar_t* className = appearance.className ? appearance.className : kClassName;
    // Each class once per process (D-145: the changelog window has its own).
    static std::mutex registerGuard;
    static std::vector<std::wstring> registeredClasses;
    {
        std::scoped_lock lock(registerGuard);
        if (std::ranges::find(registeredClasses, std::wstring(className)) == registeredClasses.end()) {
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = &Window::windowProc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hIcon = appearance.icon;
            wc.lpszClassName = className;
            if (RegisterClassExW(&wc) == 0) {
                return fail(ErrorCode::Unknown, L"RegisterClassExW failed", L"Window::create",
                            static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
            }
            registeredClasses.emplace_back(className);
        }
    }

    m_hwnd = CreateWindowExW(0, className, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_USEDEFAULT, appearance.owner, nullptr, instance, this);
    if (!m_hwnd) {
        return fail(ErrorCode::Unknown, L"CreateWindowExW failed", L"Window::create",
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    m_dpiScale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
    m_scale = m_dpiScale * m_zoom;

    // DWM frame: dark/light system menus + border color + rounded corners (Windows 11).
    setFrameColors(appearance.darkFrame, appearance.borderColor);
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    m_background = CreateSolidBrush(appearance.background);
    setIcons(GetDpiForWindow(m_hwnd));
    // The taskbar button's progress: Explorer says when the button is there. An elevated WinLove
    // would not hear it from a non-elevated Explorer without the filter (UIPI).
    m_taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
    if (m_taskbarCreated != 0) {
        ChangeWindowMessageFilterEx(m_hwnd, m_taskbarCreated, MSGFLT_ALLOW, nullptr);
    }

    // Initial size in DIPs, clamped to 90% of the monitor work area, centered.
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT work = monitor.rcWork;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    // D-114: the whole window, frame and status bar included, stays inside the work area (above the
    // taskbar) — on a small or zoomed screen 90 % of it, never the asked size beyond it.
    // The asked size is the client area's (--size=1440x900 gives a 1440×900 DIP client); the
    // invisible side and bottom frame come on top of it.
    const int frame = frameThicknessPx();
    const int width =
        std::min(static_cast<int>(std::lround(initialSize.width * m_scale)) + 2 * frame, workW * 9 / 10);
    const int height = std::min(static_cast<int>(std::lround(initialSize.height * m_scale)) + frame, workH * 9 / 10);
    SetWindowPos(m_hwnd, nullptr, work.left + (workW - width) / 2, work.top + (workH - height) / 2, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    updateSize();
    // What was posted before there was a window (jobs started while the interface was built) had
    // no message to ride on and waited for the next post() (audit UI-03).
    bool waiting = false;
    {
        std::scoped_lock lock(m_postMutex);
        waiting = !m_posted.empty();
    }
    if (waiting) {
        PostMessageW(m_hwnd, kPostMessage, 0, 0);
    }
    return {};
}

void Window::setZoom(float zoom) {
    zoom = std::clamp(zoom, 1.0f, 2.5f);
    if (zoom == m_zoom) {
        return;
    }
    m_zoom = zoom;
    m_scale = m_dpiScale * m_zoom;
    if (!m_hwnd) {
        return;
    }
    updateSize();
    if (m_callbacks.resized) {
        m_callbacks.resized(m_clientSize, m_scale);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::show(bool maximized) {
    ShowWindow(m_hwnd, maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(m_hwnd);
}

int Window::runMessageLoop() {
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void Window::invalidate() {
    // The interface is built before the window exists, and InvalidateRect(NULL) means "redraw every
    // window of the desktop" (audit UI-03). The first WM_PAINT comes by itself.
    if (!m_hwnd) {
        return;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::minimize() {
    ShowWindow(m_hwnd, SW_MINIMIZE);
}

void Window::toggleMaximize() {
    ShowWindow(m_hwnd, isMaximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void Window::close() {
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
}

void Window::setTaskbarProgress(std::optional<float> fraction) {
    m_taskbarProgress = fraction;
    applyTaskbarProgress();
}

void Window::applyTaskbarProgress() {
    if (!m_taskbar || !m_hwnd) {
        return;
    }
    if (!m_taskbarProgress) {
        m_taskbar->SetProgressState(m_hwnd, TBPF_NOPROGRESS);
    } else if (*m_taskbarProgress < 0.0f) {
        m_taskbar->SetProgressState(m_hwnd, TBPF_INDETERMINATE);
    } else {
        constexpr ULONGLONG kTotal = 1000;
        const auto done = static_cast<ULONGLONG>(std::lround(std::clamp(*m_taskbarProgress, 0.0f, 1.0f) * kTotal));
        m_taskbar->SetProgressState(m_hwnd, TBPF_NORMAL);
        m_taskbar->SetProgressValue(m_hwnd, done, kTotal);
    }
}

void Window::releaseTaskbar() {
    if (m_taskbar) {
        m_taskbar->Release();
        m_taskbar = nullptr;
    }
}

void Window::flashIfInactive() {
    if (!m_hwnd || GetForegroundWindow() == m_hwnd) {
        return;
    }
    FLASHWINFO flash{sizeof(flash), m_hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0};
    FlashWindowEx(&flash);
}

void Window::bringToFront() {
    if (!m_hwnd) {
        return;
    }
    if (IsIconic(m_hwnd)) {
        ShowWindow(m_hwnd, SW_RESTORE);
    }
    SetForegroundWindow(m_hwnd);
}

PostHold::PostHold(HWND window) noexcept : m_window(window) {
    ++g_postHolds;
}

PostHold::~PostHold() {
    if (--g_postHolds == 0 && m_window) {
        PostMessageW(m_window, kPostMessage, 0, 0); // run what waited
    }
}

void Window::post(std::function<void()> fn) {
    {
        std::scoped_lock lock(m_postMutex);
        m_posted.push_back(std::move(fn));
    }
    if (m_hwnd) {
        PostMessageW(m_hwnd, kPostMessage, 0, 0);
    }
}

void Window::setTimer(UINT id, UINT ms) {
    // Without a window SetTimer makes a thread timer under a new id that nothing can stop.
    if (!m_hwnd) {
        return;
    }
    SetTimer(m_hwnd, id, ms, nullptr);
}

void Window::stopTimer(UINT id) {
    if (!m_hwnd) {
        return;
    }
    KillTimer(m_hwnd, id);
}

void Window::setFrameColors(bool dark, COLORREF border) {
    const BOOL darkMode = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

bool Window::isMaximized() const noexcept {
    return IsZoomed(m_hwnd) != FALSE;
}

int Window::frameThicknessPx() const {
    const UINT dpi = GetDpiForWindow(m_hwnd);
    return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

void Window::setFileDropHandler(std::function<void(std::vector<std::filesystem::path>, PointF)> handler) {
    m_fileDrop = std::move(handler);
    DragAcceptFiles(m_hwnd, m_fileDrop ? TRUE : FALSE);
    if (m_fileDrop) {
        // Let a non-elevated Explorer deliver drops to this (elevated) window.
        constexpr UINT kCopyGlobalData = 0x0049;
        for (const UINT message : {static_cast<UINT>(WM_DROPFILES), static_cast<UINT>(WM_COPYDATA), kCopyGlobalData}) {
            ChangeWindowMessageFilterEx(m_hwnd, message, MSGFLT_ALLOW, nullptr);
        }
    }
}

PointF Window::toClientDips(POINT screen) const {
    ScreenToClient(m_hwnd, &screen);
    return {static_cast<float>(screen.x) / m_scale, static_cast<float>(screen.y) / m_scale};
}

void Window::updateSize() {
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    m_widthPx = static_cast<UINT>(rc.right - rc.left);
    m_heightPx = static_cast<UINT>(rc.bottom - rc.top);
    m_clientSize = {static_cast<float>(m_widthPx) / m_scale, static_cast<float>(m_heightPx) / m_scale};
}

void Window::trackLeave(bool nonClient) {
    TRACKMOUSEEVENT track{sizeof(track)};
    track.dwFlags = TME_LEAVE | (nonClient ? TME_NONCLIENT : 0u);
    track.hwndTrack = m_hwnd;
    TrackMouseEvent(&track);
}

LRESULT CALLBACK Window::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Window::hitTest(LPARAM lParam) {
    // Side and bottom resize borders are real (invisible) frame: let Windows classify them.
    const LRESULT frameHit = DefWindowProcW(m_hwnd, WM_NCHITTEST, 0, lParam);
    if (frameHit != HTCLIENT) {
        return frameHit;
    }
    POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    POINT client = screen;
    ScreenToClient(m_hwnd, &client);
    // The top border lives inside our client area: a thin band resizes (interaction.md: "üst 4px HTTOP").
    const int topBand = GetSystemMetricsForDpi(SM_CYFRAME, GetDpiForWindow(m_hwnd));
    if (!isMaximized() && client.y < topBand) {
        const int corner = frameThicknessPx();
        if (client.x < corner) {
            return HTTOPLEFT;
        }
        if (client.x >= static_cast<int>(m_widthPx) - corner) {
            return HTTOPRIGHT;
        }
        return HTTOP;
    }
    const HitZone zone = m_callbacks.hitTest ? m_callbacks.hitTest(toClientDips(screen)) : HitZone::Client;
    return hitFromZone(zone);
}

LRESULT Window::handle(UINT message, WPARAM wParam, LPARAM lParam) {
    auto pointer = [&](PointerAction action, PointF position, HitZone zone) {
        if (m_callbacks.pointer) {
            m_callbacks.pointer({action, position, zone});
        }
    };
    auto clientPoint = [&] {
        return PointF{static_cast<float>(GET_X_LPARAM(lParam)) / m_scale,
                      static_cast<float>(GET_Y_LPARAM(lParam)) / m_scale};
    };
    auto screenPoint = [&] { return toClientDips({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}); };

    if (m_taskbarCreated != 0 && message == m_taskbarCreated) {
        // The button exists (again, after an Explorer restart): a new taskbar object, then the progress.
        releaseTaskbar();
        ITaskbarList3* taskbar = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(TaskbarList), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar)))) {
            if (SUCCEEDED(taskbar->HrInit())) {
                m_taskbar = taskbar;
                applyTaskbarProgress();
            } else {
                taskbar->Release();
            }
        }
        return 0;
    }
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const LONG originalTop = params->rgrc[0].top;
            const LRESULT result = DefWindowProcW(m_hwnd, message, wParam, lParam);
            if (result != 0) {
                return result;
            }
            // Keep the default side/bottom frame, but give the caption strip to the client area.
            params->rgrc[0].top = originalTop;
            if (isMaximized()) {
                // A maximized window hangs its frame off-screen; keep our title bar on-screen.
                params->rgrc[0].top += frameThicknessPx();
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return hitTest(lParam);
    case WM_NCACTIVATE:
        // lParam -1: update activation state without letting Windows repaint a classic frame.
        return DefWindowProcW(m_hwnd, message, wParam, -1);
    case WM_ACTIVATE:
        if (m_callbacks.activated) {
            m_callbacks.activated(LOWORD(wParam) != WA_INACTIVE);
        }
        return 0;
    case WM_ERASEBKGND:
        if (m_background) {
            RECT rc{};
            GetClientRect(m_hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, m_background);
        }
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        EndPaint(m_hwnd, &ps);
        if (m_callbacks.paint) {
            m_callbacks.paint();
        }
        return 0;
    }
    case WM_SIZE: {
        updateSize();
        if (m_callbacks.resized && wParam != SIZE_MINIMIZED) {
            m_callbacks.resized(m_clientSize, m_scale);
        }
        const bool maximized = wParam == SIZE_MAXIMIZED;
        if (wParam != SIZE_MINIMIZED && maximized != m_wasMaximized) {
            m_wasMaximized = maximized;
            if (m_callbacks.maximizedChanged) {
                m_callbacks.maximizedChanged(maximized);
            }
        }
        return 0;
    }
    case WM_DPICHANGED: {
        m_dpiScale = static_cast<float>(HIWORD(wParam)) / 96.0f;
        m_scale = m_dpiScale * m_zoom;
        setIcons(HIWORD(wParam)); // the taskbar / Alt+Tab icon at the new DPI
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        updateSize();
        if (m_callbacks.resized) {
            m_callbacks.resized(m_clientSize, m_scale);
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const float scale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f * m_zoom;
        const int sideFrames = 2 * frameThicknessPx();
        info->ptMinTrackSize.x = static_cast<LONG>(std::lround(m_minimum.width * scale)) + sideFrames;
        info->ptMinTrackSize.y = static_cast<LONG>(std::lround(m_minimum.height * scale)) + sideFrames / 2;
        // D-114: a minimum larger than the work area pushed the bottom (the status bar) under the
        // taskbar on small or zoomed screens: never ask for more than the monitor's work area.
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
            const LONG workW = monitor.rcWork.right - monitor.rcWork.left;
            const LONG workH = monitor.rcWork.bottom - monitor.rcWork.top;
            info->ptMinTrackSize.x = std::min(info->ptMinTrackSize.x, workW);
            info->ptMinTrackSize.y = std::min(info->ptMinTrackSize.y, workH);
        }
        return 0;
    }

    // ---- client-area pointer ------------------------------------------------------------
    case WM_MOUSEMOVE:
        trackLeave(false);
        pointer(PointerAction::Move, clientPoint(), HitZone::Client);
        return 0;
    case WM_MOUSELEAVE:
        pointer(PointerAction::Leave, {-1, -1}, HitZone::Client);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(m_hwnd);
        pointer(PointerAction::Down, clientPoint(), HitZone::Client);
        return 0;
    case WM_LBUTTONUP:
        // Up first: ReleaseCapture sends WM_CAPTURECHANGED, which cancels a press still pending.
        pointer(PointerAction::Up, clientPoint(), HitZone::Client);
        m_releasingCapture = true;
        ReleaseCapture();
        m_releasingCapture = false;
        return 0;
    case WM_RBUTTONUP:
        if (m_callbacks.contextMenu) {
            m_callbacks.contextMenu(clientPoint());
        }
        return 0;
    case WM_CAPTURECHANGED:
        // Capture taken away mid-press (Alt+Tab, UAC, a dialog): drop the press instead of
        // leaving a widget stuck in its pressed / dragging state.
        if (reinterpret_cast<HWND>(lParam) != m_hwnd && !m_releasingCapture) {
            pointer(PointerAction::Cancel, {-1, -1}, HitZone::Client);
        }
        return 0;

    // ---- non-client pointer: our caption buttons -----------------------------------------
    // Windows must not see button presses, or it paints classic caption buttons on top of ours.
    case WM_NCMOUSEMOVE:
        trackLeave(true);
        pointer(PointerAction::Move, screenPoint(), zoneFromHit(wParam));
        if (isCaptionButton(wParam)) {
            return 0;
        }
        break;
    case WM_NCMOUSELEAVE:
        pointer(PointerAction::Leave, {-1, -1}, HitZone::Client);
        break;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (isCaptionButton(wParam)) {
            pointer(PointerAction::Down, screenPoint(), zoneFromHit(wParam));
            return 0;
        }
        break;
    case WM_NCLBUTTONUP:
        if (isCaptionButton(wParam)) {
            pointer(PointerAction::Up, screenPoint(), zoneFromHit(wParam));
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
        // Alt+↓ / Alt+↑ open and close dropdowns; every other Alt combination (Alt+F4, Alt+Space)
        // stays with Windows.
        if ((wParam == VK_DOWN || wParam == VK_UP) && m_callbacks.keyDown) {
            auto down = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
            m_callbacks.keyDown({static_cast<UINT>(wParam), down(VK_CONTROL), down(VK_SHIFT), true});
            return 0;
        }
        // F10 is a system key even without Alt: Shift+F10 (the context menu of the focused widget,
        // Host::onKeyDown) never arrived as WM_KEYDOWN (audit D10). Plain F10 stays with Windows.
        if (wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000) != 0 && (GetKeyState(VK_MENU) & 0x8000) == 0 &&
            m_callbacks.keyDown) {
            m_callbacks.keyDown({static_cast<UINT>(wParam), (GetKeyState(VK_CONTROL) & 0x8000) != 0, true, false,
                                 (lParam & 0x40000000) != 0});
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (m_callbacks.keyDown) {
            auto down = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
            m_callbacks.keyDown({static_cast<UINT>(wParam), down(VK_CONTROL), down(VK_SHIFT), down(VK_MENU), (lParam & 0x40000000) != 0});
        }
        return 0;
    case kPostMessage: {
        if (g_postHolds.load() > 0) {
            return 0; // a system dialog is open: PostHold posts again when it closes
        }
        // One at a time, each taken from the shared queue right before it runs: an item that opens
        // a nested message loop (a message box) lets that loop continue with the next ones in
        // order. Taken as a whole batch, the older items waited here while newer posts ran inside
        // the nested loop — "finished" before the progress posted ahead of it (audit UI-08).
        // At most what was queued on entry: a worker that keeps posting must not starve input.
        std::size_t budget = 0;
        {
            std::scoped_lock lock(m_postMutex);
            budget = m_posted.size();
        }
        for (; budget > 0; --budget) {
            if (g_postHolds.load() > 0) {
                return 0; // the item just run opened a hold: PostHold posts again when it ends
            }
            std::function<void()> fn;
            {
                std::scoped_lock lock(m_postMutex);
                if (m_posted.empty()) {
                    break; // a nested loop ran them
                }
                fn = std::move(m_posted.front());
                m_posted.pop_front();
            }
            fn();
        }
        return 0; // what was posted meanwhile brought its own message
    }
    case WM_TIMER:
        if (m_callbacks.timer) {
            m_callbacks.timer(static_cast<UINT>(wParam));
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && m_callbacks.cursor) {
            POINT screen{};
            GetCursorPos(&screen);
            const Cursor cursor = m_callbacks.cursor(toClientDips(screen));
            const wchar_t* id = cursor == Cursor::Hand     ? IDC_HAND
                                : cursor == Cursor::SizeWE ? IDC_SIZEWE
                                : cursor == Cursor::IBeam  ? IDC_IBEAM
                                                           : IDC_ARROW;
            SetCursor(LoadCursorW(nullptr, id));
            return TRUE;
        }
        break;
    case WM_MOUSEWHEEL:
        if (m_callbacks.wheel) {
            UINT linesPerNotch = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &linesPerNotch, 0);
            if (linesPerNotch == WHEEL_PAGESCROLL) {
                linesPerNotch = 20;
            }
            const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            m_callbacks.wheel(toClientDips({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}),
                              notches * static_cast<float>(linesPerNotch));
        }
        return 0;
    case WM_CHAR:
        // Control characters (Ctrl+A = 0x01, Backspace, Enter, Esc, Tab) arrive as WM_KEYDOWN.
        if (m_callbacks.character && wParam >= 0x20 && wParam != 0x7F) {
            m_callbacks.character(static_cast<wchar_t>(wParam));
        }
        return 0;
    case WM_DROPFILES: {
        const auto drop = reinterpret_cast<HDROP>(wParam);
        std::vector<std::filesystem::path> files;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            std::wstring name(DragQueryFileW(drop, i, nullptr, 0), wchar_t{});
            DragQueryFileW(drop, i, name.data(), static_cast<UINT>(name.size() + 1));
            files.emplace_back(std::move(name));
        }
        POINT at{};
        DragQueryPoint(drop, &at);
        ClientToScreen(m_hwnd, &at);
        DragFinish(drop);
        if (m_fileDrop) {
            m_fileDrop(std::move(files), toClientDips(at));
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (m_callbacks.settingsChanged) {
            m_callbacks.settingsChanged();
        }
        break;
    case WM_CLOSE:
        if (m_callbacks.closeRequested && !m_callbacks.closeRequested()) {
            return 0;
        }
        break;
    case WM_QUERYENDSESSION:
        // With ShutdownBlockReasonCreate set, Windows shows the reason and lets the user decide.
        if (m_callbacks.endSessionBlocked && m_callbacks.endSessionBlocked()) {
            return FALSE;
        }
        return TRUE;
    case WM_DESTROY:
        releaseTaskbar();
        if (m_background) {
            DeleteObject(m_background);
            m_background = nullptr;
        }
        if (m_quitOnClose) {
            PostQuitMessage(0);
        }
        return 0;
    case WM_NCDESTROY: {
        // D-145: a second window's handle is dead from here on; its owner hears of it (and lets go of
        // this object later, never inside the window procedure). The main window keeps its old
        // handle value as it always did (what runs after the message loop reads it).
        if (m_quitOnClose) {
            break;
        }
        const HWND hwnd = m_hwnd;
        m_hwnd = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (auto destroyed = m_callbacks.destroyed) {
            destroyed();
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, message, wParam, lParam);
}

} // namespace wl::ui
