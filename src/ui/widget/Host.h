#pragma once
// Owns the widget tree of one window and connects it to the platform:
// pointer routing (hover, press/capture, click), keyboard focus (Tab / Shift+Tab, focus ring only
// after keyboard navigation — interaction.md "FocusVisible"), tooltips (400 ms), animation frames.
#include "ui/widget/Widget.h"

#include <functional>
#include <vector>

namespace wl::ui {

struct HostServices {
    std::function<void()> requestFrame;                   // schedule a repaint
    std::function<void(UINT id, UINT ms)> startTimer;     // one-shot style: Host stops it on fire
    std::function<void(UINT id)> stopTimer;
    const TextStyles* text = nullptr;                     // text measurement for measure()
    // How far apart two clicks may be and still make a double click, in DIPs: the window's DPI
    // (GetSystemMetricsForDpi), not the system DPI's pixels. Unset: the system metric as it comes.
    std::function<SizeF()> doubleClickArea;
};

class Host {
public:
    explicit Host(HostServices services) : m_services(std::move(services)) {}
    ~Host();

    void setRoot(std::unique_ptr<Widget> root);
    [[nodiscard]] Widget* root() const noexcept { return m_root.get(); }
    [[nodiscard]] const TextStyles& text() const { return *m_services.text; }

    void layout(SizeF size);
    [[nodiscard]] SizeF size() const noexcept { return m_size; }
    // Advances animations, paints the tree, the focus ring and the tooltip.
    void paint(Canvas& canvas);

    [[nodiscard]] HitZone windowZone(PointF p) const;
    [[nodiscard]] Cursor cursorAt(PointF p) const;
    void onPointer(const PointerEvent& event);
    // Returns true when the key was consumed by the tree (focus navigation or a widget).
    bool onKeyDown(const KeyEvent& key);
    // Right click: the widget under `p`, then its parents, until one opens a menu.
    bool onContextMenu(PointF p);
    void onWheel(PointF p, float lines);
    void onChar(wchar_t ch);
    void onTimer(UINT id);

    // Modal layer (dialogs, command palette): covers the window, gets a scrim, traps focus and
    // input. The widget's bounds are set to the whole window. popModal restores the old focus.
    // `scrim` false: popups (menus, dropdown lists) — same input trapping, no dimming; the popup
    // widget covers the window and closes itself on a click outside its panel.
    // `owner`: the widget whose code opened the modal and whose state its handlers use. When the
    // owner is destroyed (its page replaced by a finished job, a file dropped on the window) the
    // modal goes with it instead of calling into freed memory.
    Widget& pushModal(std::unique_ptr<Widget> modal, Widget* initialFocus = nullptr, bool scrim = true,
                      Widget* owner = nullptr);
    void popModal(Widget* modal);
    [[nodiscard]] bool hasModal() const noexcept { return !m_modals.empty(); }

    void setFocus(Widget* widget, bool visible);
    [[nodiscard]] Widget* focused() const noexcept { return m_focused; }
    void focusNext(bool reverse);

    // ---- used by Widget -------------------------------------------------------------------
    void requestFrame();
    void startAnimating(Widget* widget);
    void forget(Widget* widget);  // widget destroyed: its slots are cleared, nothing is called on it
    void release(Widget* widget); // widget hidden or disabled: hover / press / focus end normally
    // The hovered widget's tooltip changed under the pointer (a widget with a tooltip per item: the
    // item under the pointer is another one): the shown one goes, the new one comes after the delay.
    void tooltipChanged(Widget* widget);

    static constexpr UINT kTooltipTimer = 1;
    static constexpr UINT kFrameTimer = 5; // the next frame of slow animations (Widget::tickIntervalMs)

private:
    void setHovered(Widget* widget);
    void setPressed(Widget* widget);
    void clearSlots(Widget* widget); // every slot pointing into `widget`
    void hideTooltip();
    bool m_ticking = false; // inside paint()'s tick loop: frame requests are that frame
    void paintTooltip(Canvas& canvas);
    void collectFocusable(Widget* widget, std::vector<Widget*>& out) const;
    [[nodiscard]] Widget* inputRoot() const noexcept; // top modal or the root

    HostServices m_services;
    std::unique_ptr<Widget> m_root;
    struct Modal {
        std::unique_ptr<Widget> widget;
        Widget* previousFocus;
        bool scrim = true;
        Widget* owner = nullptr;
    };
    std::vector<Modal> m_modals;
    SizeF m_size{};
    Widget* m_hovered = nullptr;
    Widget* m_pressed = nullptr;
    Widget* m_focused = nullptr;
    bool m_focusVisible = false;
    std::vector<Widget*> m_animating;
    std::vector<Widget*> m_tickList;          // paint(): this frame's ticks (kept: no allocation per frame)
    std::vector<const Widget*> m_ringClips;   // paint(): the focus ring's clipping ancestors
    Widget* m_tooltipOwner = nullptr;
    bool m_tooltipVisible = false;
    PointF m_tooltipAnchor{};
    float m_tooltipFlipY = 0; // the anchor's top: a tooltip that would leave the window goes above it
    PointF m_lastPointer{-1, -1};
    double m_lastClickTime = 0;
    Widget* m_lastClickWidget = nullptr;
    Widget* m_upTarget = nullptr; // the widget whose onPointerUp is running: null again if it destroyed itself
    PointF m_lastClickPos{-1000, -1000};
};

} // namespace wl::ui
