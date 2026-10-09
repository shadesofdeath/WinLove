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
#include <vector>

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
};

struct WindowAppearance {
    HICON icon = nullptr;
    bool darkFrame = true;       // DWM immersive dark mode (system menu, border shading)
    COLORREF borderColor = 0;    // Windows 11 1px window border
    COLORREF background = 0;     // shown before the first D2D frame (avoids a white flash)
};

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    [[nodiscard]] Result<void> create(const wchar_t* title, SizeF initialSize, SizeF minimumSize,
                                      WindowCallbacks callbacks, const WindowAppearance& appearance);
    void show();
    [[nodiscard]] static int runMessageLoop();

    void invalidate();
    void minimize();
    void toggleMaximize();
    void close();
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

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT hitTest(LPARAM lParam);
    void updateSize();
    [[nodiscard]] PointF toClientDips(POINT screen) const;
    [[nodiscard]] int frameThicknessPx() const;
    void trackLeave(bool nonClient);

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
    std::mutex m_postMutex;
    std::deque<std::function<void()>> m_posted;
    bool m_wasMaximized = false;
};

} // namespace wl::ui
