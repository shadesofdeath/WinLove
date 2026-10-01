#pragma once
// Base of every UI element. A retained tree: parents own children, layout assigns each child an
// absolute rectangle in window DIPs, the Host (Host.h) routes pointer/keyboard/focus and paints.
//
// Rules (docs/CONVENTIONS.md "UI kodu"):
// - paint() only draws; no layout work, no allocation-heavy work.
// - A widget calls invalidate() when its look changes; animations go through Tweens + animate().
// - Composite widgets usually paint their own parts instead of spawning a child per icon/label.
#include "ui/Geometry.h"
#include "ui/platform/Window.h"
#include "ui/render/Canvas.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace wl::ui {

class Host;

// UI Automation role (08_implementation/accessibility.md); provider lands with Faz 4 a11y pass.
enum class AccessRole : std::uint8_t { None, Group, Button, Text, TabItem, CheckBox, Edit, List, ListItem, Tree, TreeItem };

class Widget {
public:
    Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    virtual ~Widget();

    // ---- tree ------------------------------------------------------------------------------
    template <class T, class... Args>
    T& add(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *child;
        addChild(std::move(child));
        return ref;
    }
    void addChild(std::unique_ptr<Widget> child);
    void clearChildren();
    // Destroys `child` (must be a direct child).
    void removeChild(Widget* child);
    // Moves a direct child to the end: painted last, hit-tested first (toasts, overlays).
    void bringToFront(Widget* child);
    [[nodiscard]] std::span<const std::unique_ptr<Widget>> children() const noexcept { return m_children; }
    [[nodiscard]] Widget* parent() const noexcept { return m_parent; }
    [[nodiscard]] Host* host() const noexcept;

    // ---- geometry --------------------------------------------------------------------------
    [[nodiscard]] RectF bounds() const noexcept { return m_bounds; }
    // Sets the rectangle and runs layout() so children follow.
    void setBounds(RectF bounds);
    // Preferred size within `available` (0 means "no preference" on that axis).
    [[nodiscard]] virtual SizeF measure(SizeF available);
    // Position children inside bounds(). Default: nothing.
    virtual void layout() {}

    // ---- state -----------------------------------------------------------------------------
    void setVisible(bool visible);
    [[nodiscard]] bool visible() const noexcept { return m_visible; }
    void setEnabled(bool enabled);
    [[nodiscard]] bool enabled() const noexcept; // false if any ancestor is disabled
    void setFocusable(bool focusable) noexcept { m_focusable = focusable; }
    [[nodiscard]] bool canFocus() const noexcept { return m_focusable && m_visible && enabled(); }
    // Focusable but skipped by Tab (roving focus inside lists/nav: arrows move, Tab leaves).
    void setTabStop(bool value) noexcept { m_tabStop = value; }
    [[nodiscard]] bool tabStop() const noexcept { return m_tabStop; }
    // Pointer events pass through widgets that are not hit-testable (decorative children).
    void setHitTestVisible(bool value) noexcept { m_hitTestVisible = value; }

    [[nodiscard]] bool hovered() const noexcept { return m_hovered || m_forcedHover; }
    [[nodiscard]] bool pressed() const noexcept { return (m_pressed && m_hovered) || m_forcedPress; }
    // The pointer went down on this widget and is still down, wherever it is now (drags).
    [[nodiscard]] bool captured() const noexcept { return m_pressed; }
    [[nodiscard]] bool focused() const noexcept { return m_focused; }
    // Gallery / --render state checks: show hover/press without a pointer.
    void forceVisualState(bool hover, bool press) noexcept;

    void setTooltip(std::wstring text) { m_tooltip = std::move(text); }
    [[nodiscard]] const std::wstring& tooltip() const noexcept { return m_tooltip; }

    void setAccessible(AccessRole role, std::wstring name) {
        m_role = role;
        m_accessibleName = std::move(name);
    }

    // ---- painting --------------------------------------------------------------------------
    void paintTree(Canvas& canvas);
    virtual void paint(Canvas& /*canvas*/) {}
    virtual void paintOverlay(Canvas& /*canvas*/) {} // after children
    [[nodiscard]] virtual bool clipsChildren() const noexcept { return false; }
    // Focus ring geometry (1px accent.focus, 1px outside, radius + 1).
    [[nodiscard]] virtual RectF focusRect() const { return m_bounds; }
    [[nodiscard]] virtual float focusRadius() const { return tokens::radius::r2; }
    void invalidate();

    // ---- hit testing -------------------------------------------------------------------------
    // Deepest visible, hit-testable widget under `p`, or nullptr.
    [[nodiscard]] virtual Widget* hitTest(PointF p);
    // What the window frame should treat this area as (title bar drag, caption buttons).
    [[nodiscard]] virtual HitZone windowZone() const { return HitZone::Client; }
    [[nodiscard]] virtual Cursor cursor() const { return Cursor::Arrow; }

    // ---- events (called by Host) ---------------------------------------------------------------
    virtual void onHoverChanged(bool /*hovered*/) { invalidate(); }
    virtual void onPressedChanged(bool /*pressed*/) { invalidate(); }
    virtual void onPointerDown(PointF /*p*/) {}
    virtual void onPointerMove(PointF /*p*/) {}
    virtual void onPointerUp(PointF /*p*/) {}
    virtual void onClick() {}
    virtual void onDoubleClick() {}
    // Return true when handled; unhandled keys bubble to the parent.
    virtual bool onKeyDown(const KeyEvent& /*key*/) { return false; }
    // Wheel over this widget (lines, + = up); unhandled bubbles to the parent.
    virtual bool onWheel(PointF /*p*/, float /*lines*/) { return false; }
    // Right click at `p` (or the Menu key / Shift+F10 with `p` inside the focus rectangle):
    // open a context menu and return true; unhandled bubbles to the parent.
    virtual bool onContextMenu(PointF /*p*/) { return false; }
    // Typed character for the focused widget (text boxes); unhandled bubbles to the parent.
    virtual bool onChar(wchar_t /*ch*/) { return false; }
    virtual void onFocusChanged(bool /*focused*/) { invalidate(); }

    // ---- animation ---------------------------------------------------------------------------
    // Called every frame while registered through animate(). Return true to keep ticking.
    virtual bool tick(double /*nowMs*/) { return false; }
    void animate(); // request ticks from the host

private:
    friend class Host;
    void setHostRecursive(Host* host);

    Widget* m_parent = nullptr;
    Host* m_host = nullptr;
    std::vector<std::unique_ptr<Widget>> m_children;
    RectF m_bounds{};
    std::wstring m_tooltip;
    std::wstring m_accessibleName;
    AccessRole m_role = AccessRole::None;
    bool m_visible = true;
    bool m_enabled = true;
    bool m_focusable = false;
    bool m_tabStop = true;
    bool m_hitTestVisible = true;
    bool m_animationPending = false; // animate() before there was a host to tick it
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_focused = false;
    bool m_forcedHover = false;
    bool m_forcedPress = false;
};

} // namespace wl::ui
