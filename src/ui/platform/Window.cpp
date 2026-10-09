#include "ui/platform/Window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

constexpr wchar_t kClassName[] = L"WinLove.Window";
constexpr UINT kPostMessage = WM_APP + 1;

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
    if (m_hwnd) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
    }
}

Result<void> Window::create(const wchar_t* title, SizeF initialSize, SizeF minimumSize, WindowCallbacks callbacks,
                            const WindowAppearance& appearance) {
    m_callbacks = std::move(callbacks);
    m_minimum = minimumSize;
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    static const bool registered = [&] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &Window::windowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = appearance.icon;
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered) {
        return fail(ErrorCode::Unknown, L"RegisterClassExW failed", L"Window::create",
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }

    m_hwnd = CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_USEDEFAULT, nullptr, nullptr, instance, this);
    if (!m_hwnd) {
        return fail(ErrorCode::Unknown, L"CreateWindowExW failed", L"Window::create",
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }
    m_scale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;

    // DWM frame: dark/light system menus + border color + rounded corners (Windows 11).
    setFrameColors(appearance.darkFrame, appearance.borderColor);
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    m_background = CreateSolidBrush(appearance.background);

    // Initial size in DIPs, clamped to 90% of the monitor work area, centered.
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT work = monitor.rcWork;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int width = std::min(static_cast<int>(std::lround(initialSize.width * m_scale)), workW * 9 / 10);
    const int height = std::min(static_cast<int>(std::lround(initialSize.height * m_scale)), workH * 9 / 10);
    SetWindowPos(m_hwnd, nullptr, work.left + (workW - width) / 2, work.top + (workH - height) / 2, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    updateSize();
    return {};
}

void Window::show() {
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
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
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::minimize() {
    ShowWindow(m_hwnd, SW_MINIMIZE);
}

void Window::toggleMaximize() {
    ShowWindow(m_hwnd, isMaximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void Window::close() {
    PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
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
    SetTimer(m_hwnd, id, ms, nullptr);
}

void Window::stopTimer(UINT id) {
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
        m_scale = static_cast<float>(HIWORD(wParam)) / 96.0f;
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
        const float scale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
        const int sideFrames = 2 * frameThicknessPx();
        info->ptMinTrackSize.x = static_cast<LONG>(std::lround(m_minimum.width * scale)) + sideFrames;
        info->ptMinTrackSize.y = static_cast<LONG>(std::lround(m_minimum.height * scale)) + sideFrames / 2;
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
        break;
    case WM_KEYDOWN:
        if (m_callbacks.keyDown) {
            auto down = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
            m_callbacks.keyDown({static_cast<UINT>(wParam), down(VK_CONTROL), down(VK_SHIFT), down(VK_MENU), (lParam & 0x40000000) != 0});
        }
        return 0;
    case kPostMessage: {
        std::deque<std::function<void()>> batch;
        {
            std::scoped_lock lock(m_postMutex);
            batch.swap(m_posted);
        }
        for (auto& fn : batch) {
            fn();
        }
        return 0;
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
        if (m_background) {
            DeleteObject(m_background);
            m_background = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, message, wParam, lParam);
}

} // namespace wl::ui
