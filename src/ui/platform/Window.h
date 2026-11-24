#pragma once
// Top-level HWND with a custom-drawn title bar (docs: interaction.md "Title bar (custom)").
// - WM_NCCALCSIZE: the client area covers the whole window except the invisible side borders,
//   so we draw the title bar ourselves while DWM keeps shadow, rounded corners and resize edges.
// - WM_NCHITTEST: asks the owner which zone a point is in; caption buttons report
//   HTMINBUTTON/HTMAXBUTTON/HTCLOSE so Windows 11 Snap Layouts appear on the maximize button.
// Everything the owner sees is in DIPs.
#include "base/Result.h"
#include "ui/Geometry.h"

#include <windows.h>

#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

struct ITaskbarList3;

namespace wl::ui {

enum class HitZone : std::uint8_t { Client, Caption, MinimizeButton, MaximizeButton, CloseButton };

enum class PointerAction : std::uint8_t { Move, Leave, Down, Up, Cancel }; // Cancel: capture lost mid-press

enum class Cursor : std::uint8_t { Arrow, Hand, SizeWE, IBeam };

struct PointerEvent {
    PointerAction action;
    PointF position; // client DIPs
    HitZone zone;    // zone under the pointer (Client for client-area messages)
};

struct KeyEvent {
    UINT virtualKey;
    bool ctrl;
    bool shift;
    bool alt;
    bool repeat = false; // auto-repeat of a held key (WM_KEYDOWN lParam bit 30)
};

struct WindowCallbacks {
    std::function<void()> paint;
    std::function<void(SizeF size, float scale)> resized; // also fires on DPI change
    std::function<HitZone(PointF)> hitTest;
    std::function<void(const PointerEvent&)> pointer;
    std::function<void(PointF)> contextMenu; // right button released in the client area (client DIPs)
    std::function<void(bool active)> activated;
    std::function<void(bool maximized)> maximizedChanged;
    std::function<void(const KeyEvent&)> keyDown;
    std::function<void(UINT id)> timer;
    std::function<Cursor(PointF)> cursor;        // client area only
    std::function<void()> settingsChanged;       // WM_SETTINGCHANGE (theme, animations)
    std::function<void(PointF, float lines)> wheel; // vertical wheel, in lines (+ = up), client DIPs
    std::function<void(wchar_t)> character;          // WM_CHAR, printable characters only
    std::function<bool()> closeRequested;            // WM_CLOSE: false keeps the window open
    std::function<bool()> endSessionBlocked;         // WM_QUERYENDSESSION: true asks Windows not to shut down yet
    // D-145: the window is gone (WM_NCDESTROY; closed by the user or by Windows). The owner may let go
    // of the Window object afterwards — not inside this call.
    std::function<void()> destroyed;
};

// While one lives, what is posted to a window (Window::post) waits. For system dialogs: their own
// message loop would otherwise run engine results in the middle of the code that opened the dialog
// (a finished "open source" switches the page and destroys the one waiting for a folder).
class PostHold {
public:
    explicit PostHold(HWND window) noexcept;
    ~PostHold();
    PostHold(const PostHold&) = delete;
    PostHold& operator=(const PostHold&) = delete;

private:
    HWND m_window;
};

struct WindowAppearance {
    HICON icon = nullptr;
    // The same icon as a resource (MAKEINTRESOURCE): loaded again at the window's DPI for the
    // taskbar / Alt+Tab (WM_SETICON) — the class icon is the system DPI's 32 px.
    const wchar_t* iconResource = nullptr;
    bool darkFrame = true;       // DWM immersive dark mode (system menu, border shading)
    COLORREF borderColor = 0;    // Windows 11 1px window border
    COLORREF background = 0;     // shown before the first D2D frame (avoids a white flash)
    // D-145: a second top-level window (the changelog): its own class name (the single-instance
    // check looks for the main one by its class), owned by the main window (above it, minimised with
    // it), and closing it does not end the message loop.
    const wchar_t* className = nullptr; // nullptr: "WinLove.Window"
    HWND owner = nullptr;
    bool quitOnClose = true;
};

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    [[nodiscard]] Result<void> create(const wchar_t* title, SizeF initialSize, SizeF minimumSize,
                                      WindowCallbacks callbacks, const WindowAppearance& appearance);
    // maximized: the window opens filling the screen (its normal size stays the one it was created with).
    void show(bool maximized = false);
    [[nodiscard]] static int runMessageLoop();

    void invalidate();
    void minimize();
    void toggleMaximize();
    void close();
    // D-145: restores a minimised window and brings it to the front (a second "Değişiklikleri gör").
    void bringToFront();
    // 2026-11-24: the taskbar button's progress (fraction 0…1; < 0 unmeasured; nullopt: none). Kept
    // until the button exists (Explorer's "TaskbarButtonCreated", also after Explorer restarts).
    void setTaskbarProgress(std::optional<float> fraction);
    // Flashes the taskbar button until the window comes to the front, when it is not in front now.
    void flashIfInactive();
    // Thread-safe: run `fn` on the UI thread (engine results, progress). Dropped after destroy.
    void post(std::function<void()> fn);
    void setTimer(UINT id, UINT ms);
    void stopTimer(UINT id);
    // Re-apply DWM frame colors after a theme change.
    void setFrameColors(bool dark, COLORREF border);
    // Classic WM_DROPFILES drops (used when elevated: UIPI blocks OLE drag & drop from Explorer).
    void setFileDropHandler(std::function<void(std::vector<std::filesystem::path>, PointF)> handler);

    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd; }
    [[nodiscard]] float scale() const noexcept { return m_scale; }
    // D-092: the interface drawn larger than the monitor's DPI says (density, Windows text size);
    // scale() = DPI / 96 × zoom. Lays the window out again.
    void setZoom(float zoom);
    [[nodiscard]] SizeF clientSize() const noexcept { return m_clientSize; }
    [[nodiscard]] UINT clientWidthPx() const noexcept { return m_widthPx; }
    [[nodiscard]] UINT clientHeightPx() const noexcept { return m_heightPx; }
    [[nodiscard]] bool isMaximized() const noexcept;
    // Windows' double-click rectangle at this window's DPI, in client DIPs.
    [[nodiscard]] SizeF doubleClickArea() const;

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT hitTest(LPARAM lParam);
    void updateSize();
    [[nodiscard]] PointF toClientDips(POINT screen) const;
    [[nodiscard]] int frameThicknessPx() const;
    void trackLeave(bool nonClient);
    void setIcons(UINT dpi);

    HWND m_hwnd = nullptr;
    bool m_releasingCapture = false; // our own ReleaseCapture: its WM_CAPTURECHANGED is no cancel
    WindowCallbacks m_callbacks;
    std::function<void(std::vector<std::filesystem::path>, PointF)> m_fileDrop;
    SizeF m_minimum{};
    SizeF m_clientSize{};
    UINT m_widthPx = 0;
    UINT m_heightPx = 0;
    float m_scale = 1.0f;
    float m_dpiScale = 1.0f;
    float m_zoom = 1.0f;
    HBRUSH m_background = nullptr;
    const wchar_t* m_iconResource = nullptr;
    HICON m_smallIcon = nullptr; // WM_SETICON at the window's DPI (owned)
    HICON m_bigIcon = nullptr;
    std::mutex m_postMutex;
    std::deque<std::function<void()>> m_posted;
    bool m_wasMaximized = false;
    bool m_quitOnClose = true;
    UINT m_taskbarCreated = 0;           // RegisterWindowMessage("TaskbarButtonCreated")
    ITaskbarList3* m_taskbar = nullptr;  // owned (Release in WM_DESTROY / the destructor)
    std::optional<float> m_taskbarProgress;
    void applyTaskbarProgress();
    void releaseTaskbar();
};

} // namespace wl::ui
