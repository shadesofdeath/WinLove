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
    void onTimer(UINT id);

    // Modal layer (dialogs, command palette): covers the window, gets a scrim, traps focus and
    // input. The widget's bounds are set to the whole window. popModal restores the old focus.
    Widget& pushModal(std::unique_ptr<Widget> modal, Widget* initialFocus = nullptr);
    void popModal(Widget* modal);
    [[nodiscard]] bool hasModal() const noexcept { return !m_modals.empty(); }

    void setFocus(Widget* widget, bool visible);
    [[nodiscard]] Widget* focused() const noexcept { return m_focused; }
    void focusNext(bool reverse);

    // ---- used by Widget -------------------------------------------------------------------
    void requestFrame();
    void startAnimating(Widget* widget);
    void forget(Widget* widget); // widget destroyed, hidden or disabled

    static constexpr UINT kTooltipTimer = 1;

private:
    void setHovered(Widget* widget);
    void setPressed(Widget* widget);
    void hideTooltip();
    void paintTooltip(Canvas& canvas);
    void collectFocusable(Widget* widget, std::vector<Widget*>& out) const;
    [[nodiscard]] Widget* inputRoot() const noexcept; // top modal or the root

    HostServices m_services;
    std::unique_ptr<Widget> m_root;
    struct Modal {
        std::unique_ptr<Widget> widget;
        Widget* previousFocus;
    };
    std::vector<Modal> m_modals;
    SizeF m_size{};
    Widget* m_hovered = nullptr;
    Widget* m_pressed = nullptr;
    Widget* m_focused = nullptr;
    bool m_focusVisible = false;
    std::vector<Widget*> m_animating;
    Widget* m_tooltipOwner = nullptr;
    bool m_tooltipVisible = false;
    PointF m_tooltipAnchor{};
    PointF m_lastPointer{-1, -1};
    double m_lastClickTime = 0;
    Widget* m_lastClickWidget = nullptr;
};

} // namespace wl::ui
