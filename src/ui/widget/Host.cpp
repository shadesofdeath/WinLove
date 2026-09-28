#include "ui/widget/Host.h"

#include "ui/anim/Tween.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

// contextmenu.md "Tooltip": 400 ms delay, 20px tall, padding 2 6, caption text, 4px below target.
constexpr UINT kTooltipDelayMs = 400;
constexpr float kTooltipHeight = 20.0f;
constexpr float kTooltipPaddingX = 6.0f;
constexpr float kTooltipGap = 4.0f;
constexpr float kTooltipMaxWidth = 280.0f;

bool isAncestor(const Widget* ancestor, const Widget* widget) {
    for (const Widget* w = widget; w; w = w->parent()) {
        if (w == ancestor) {
            return true;
        }
    }
    return false;
}

} // namespace

Host::~Host() {
    // Children unregister in their destructors; make that a no-op while tearing down.
    m_modals.clear();
    m_root.reset();
}

void Host::setRoot(std::unique_ptr<Widget> root) {
    m_hovered = m_pressed = m_focused = m_tooltipOwner = nullptr;
    m_animating.clear();
    m_root = std::move(root);
    if (m_root) {
        m_root->setHostRecursive(this);
        m_root->setBounds({0, 0, m_size.width, m_size.height});
    }
    requestFrame();
}

void Host::layout(SizeF size) {
    m_size = size;
    if (m_root) {
        m_root->setBounds({0, 0, size.width, size.height});
    }
    for (auto& modal : m_modals) {
        modal.widget->setBounds({0, 0, size.width, size.height});
    }
}

Widget* Host::inputRoot() const noexcept {
    return m_modals.empty() ? m_root.get() : m_modals.back().widget.get();
}

Widget& Host::pushModal(std::unique_ptr<Widget> modal, Widget* initialFocus) {
    hideTooltip();
    setHovered(nullptr);
    setPressed(nullptr);
    Widget* previous = m_focused;
    modal->setHostRecursive(this);
    modal->setBounds({0, 0, m_size.width, m_size.height});
    Widget& ref = *modal;
    m_modals.push_back({std::move(modal), previous});
    if (initialFocus) {
        setFocus(initialFocus, /*visible=*/true);
    } else {
        std::vector<Widget*> order;
        collectFocusable(&ref, order);
        setFocus(order.empty() ? nullptr : order.front(), /*visible=*/true);
    }
    requestFrame();
    return ref;
}

void Host::popModal(Widget* modal) {
    const auto it = std::ranges::find_if(m_modals, [modal](const Modal& m) { return m.widget.get() == modal; });
    if (it == m_modals.end()) {
        return;
    }
    Widget* previous = it->previousFocus;
    // Move out first: the widget's destructor calls forget(), which must not see it in m_modals.
    auto dying = std::move(it->widget);
    m_modals.erase(it);
    dying.reset();
    if (previous && previous->canFocus()) {
        setFocus(previous, m_focusVisible);
    }
    requestFrame();
}

void Host::requestFrame() {
    if (m_services.requestFrame) {
        m_services.requestFrame();
    }
}

void Host::startAnimating(Widget* widget) {
    if (std::ranges::find(m_animating, widget) == m_animating.end()) {
        m_animating.push_back(widget);
    }
    requestFrame();
}

void Host::forget(Widget* widget) {
    auto clearIfInside = [widget](Widget*& slot) {
        if (slot && isAncestor(widget, slot)) {
            slot = nullptr;
        }
    };
    clearIfInside(m_hovered);
    clearIfInside(m_pressed);
    clearIfInside(m_focused);
    clearIfInside(m_lastClickWidget);
    for (auto& modal : m_modals) {
        clearIfInside(modal.previousFocus);
    }
    if (m_tooltipOwner && isAncestor(widget, m_tooltipOwner)) {
        hideTooltip();
    }
    std::erase_if(m_animating, [widget](Widget* w) { return isAncestor(widget, w); });
}

void Host::paint(Canvas& canvas) {
    if (!m_root) {
        return;
    }
    const double now = nowMs();
    // Tick a copy: tick() may start/stop other animations.
    auto running = m_animating;
    m_animating.clear();
    for (Widget* w : running) {
        if (w->tick(now)) {
            m_animating.push_back(w);
        }
    }

    m_root->paintTree(canvas);
    for (auto& modal : m_modals) {
        canvas.fillRect({0, 0, m_size.width, m_size.height}, tokens::Color::Scrim);
        modal.widget->paintTree(canvas);
    }

    if (m_focused && m_focusVisible && m_focused->visible()) {
        const RectF r = m_focused->focusRect();
        const float offset = tokens::radius::focusOffset;
        canvas.strokeRoundRect({r.x - offset - 1, r.y - offset - 1, r.width + 2 * (offset + 1), r.height + 2 * (offset + 1)},
                               m_focused->focusRadius() + offset + 1, tokens::Color::AccentFocus);
    }
    if (m_tooltipVisible) {
        paintTooltip(canvas);
    }
    if (!m_animating.empty()) {
        requestFrame(); // keep frames coming (Present waits for vsync)
    }
}

HitZone Host::windowZone(PointF p) const {
    if (!m_root) {
        return HitZone::Client;
    }
    const Widget* hit = m_root->hitTest(p);
    const HitZone zone = hit ? hit->windowZone() : HitZone::Client;
    if (!m_modals.empty()) {
        // Modal open: the title bar still drags, caption buttons are inert (the modal owns input).
        return zone == HitZone::Caption ? HitZone::Caption : HitZone::Client;
    }
    return zone;
}

Cursor Host::cursorAt(PointF p) const {
    if (m_pressed) {
        return m_pressed->cursor(); // keep e.g. the resize cursor while dragging
    }
    const Widget* hit = inputRoot() ? inputRoot()->hitTest(p) : nullptr;
    return hit && hit->enabled() ? hit->cursor() : Cursor::Arrow;
}

void Host::setHovered(Widget* widget) {
    if (widget == m_hovered) {
        return;
    }
    if (m_hovered) {
        m_hovered->m_hovered = false;
        m_hovered->onHoverChanged(false);
    }
    m_hovered = widget;
    if (m_hovered) {
        m_hovered->m_hovered = true;
        m_hovered->onHoverChanged(true);
    }
    hideTooltip();
    if (m_hovered && !m_hovered->tooltip().empty()) {
        m_tooltipOwner = m_hovered;
        if (m_services.startTimer) { // headless renders deliver the timer by hand
            m_services.startTimer(kTooltipTimer, kTooltipDelayMs);
        }
    }
}

void Host::setPressed(Widget* widget) {
    if (widget == m_pressed) {
        return;
    }
    Widget* previous = m_pressed;
    m_pressed = widget;
    if (previous) {
        previous->m_pressed = false;
        previous->onPressedChanged(false);
    }
    if (m_pressed) {
        m_pressed->m_pressed = true;
        m_pressed->onPressedChanged(true);
    }
}

void Host::onPointer(const PointerEvent& event) {
    if (!m_root) {
        return;
    }
    Widget* hit = event.action == PointerAction::Leave ? nullptr : inputRoot()->hitTest(event.position);
    if (hit && !hit->enabled()) {
        hit = nullptr;
    }

    switch (event.action) {
    case PointerAction::Move: {
        const float dx = event.position.x - m_lastPointer.x;
        const float dy = event.position.y - m_lastPointer.y;
        m_lastPointer = event.position;
        // While pressed, hover follows the pressed widget only (drag out = visual cancel).
        setHovered(m_pressed ? (hit == m_pressed ? m_pressed : nullptr) : hit);
        if (m_pressed) {
            m_pressed->onPointerMove(event.position);
            m_pressed->invalidate();
        } else if (hit) {
            hit->onPointerMove(event.position);
        }
        if (m_tooltipVisible && std::hypot(dx, dy) > 4.0f) {
            hideTooltip();
        }
        break;
    }
    case PointerAction::Leave:
        if (!m_pressed) {
            setHovered(nullptr);
        }
        m_lastPointer = {-1, -1};
        break;
    case PointerAction::Down:
        hideTooltip();
        setHovered(hit);
        setPressed(hit);
        if (hit) {
            if (hit->canFocus()) {
                setFocus(hit, /*visible=*/false);
            }
            hit->onPointerDown(event.position);
        }
        break;
    case PointerAction::Up: {
        Widget* pressed = m_pressed;
        setPressed(nullptr);
        if (pressed) {
            pressed->onPointerUp(event.position);
            if (hit == pressed) {
                const double now = nowMs();
                const bool isDouble = m_lastClickWidget == pressed && now - m_lastClickTime < GetDoubleClickTime();
                m_lastClickWidget = pressed;
                m_lastClickTime = now;
                pressed->onClick();
                if (isDouble) {
                    pressed->onDoubleClick();
                    m_lastClickWidget = nullptr;
                }
            }
        }
        setHovered(hit);
        break;
    }
    }
}

bool Host::onKeyDown(const KeyEvent& key) {
    hideTooltip();
    if (key.virtualKey == VK_TAB && !key.ctrl && !key.alt) {
        focusNext(key.shift);
        return true;
    }
    for (Widget* w = m_focused; w; w = w->parent()) {
        if (w->onKeyDown(key)) {
            if ((key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN) && !m_focusVisible) {
                m_focusVisible = true;
                requestFrame();
            }
            return true;
        }
    }
    return false;
}

void Host::onTimer(UINT id) {
    if (id != kTooltipTimer) {
        return;
    }
    if (m_services.stopTimer) {
        m_services.stopTimer(kTooltipTimer);
    }
    if (m_tooltipOwner && m_tooltipOwner == m_hovered && !m_pressed) {
        m_tooltipVisible = true;
        m_tooltipAnchor = {m_tooltipOwner->bounds().x, m_tooltipOwner->bounds().bottom() + kTooltipGap};
        requestFrame();
    }
}

void Host::hideTooltip() {
    if (m_services.stopTimer) {
        m_services.stopTimer(kTooltipTimer);
    }
    if (m_tooltipVisible) {
        m_tooltipVisible = false;
        requestFrame();
    }
    m_tooltipOwner = nullptr;
}

void Host::paintTooltip(Canvas& canvas) {
    if (!m_tooltipOwner) {
        return;
    }
    const auto& text = m_tooltipOwner->tooltip();
    const float textWidth = std::ceil(canvas.text().measure(text, tokens::TypeStyle::Caption));
    const float width = std::min(textWidth + 2 * kTooltipPaddingX + 2, kTooltipMaxWidth);
    float x = m_tooltipAnchor.x;
    float y = m_tooltipAnchor.y;
    // Keep inside the window: shift left at the right edge, flip above at the bottom.
    x = std::clamp(x, 4.0f, std::max(4.0f, m_size.width - width - 4.0f));
    if (y + kTooltipHeight > m_size.height - 4.0f) {
        y = m_tooltipOwner->bounds().y - kTooltipGap - kTooltipHeight;
    }
    const RectF box{std::round(x), std::round(y), width, kTooltipHeight};
    canvas.dropShadow(box, tokens::radius::r2, tokens::elevation::menu);
    canvas.fillRoundRect(box, tokens::radius::r2, tokens::Color::BgOverlay);
    canvas.strokeRoundRect(box, tokens::radius::r2, tokens::Color::LineStrong);
    canvas.drawText(text, box.inset(kTooltipPaddingX + 1, 0), tokens::TypeStyle::Caption, tokens::Color::TextPrimary);
}

void Host::setFocus(Widget* widget, bool visible) {
    if (widget && !widget->canFocus()) {
        return;
    }
    const bool visibilityChanged = m_focusVisible != visible;
    m_focusVisible = visible;
    if (widget == m_focused) {
        if (visibilityChanged) {
            requestFrame();
        }
        return;
    }
    Widget* previous = m_focused;
    m_focused = widget;
    if (previous) {
        previous->m_focused = false;
        previous->onFocusChanged(false);
    }
    if (m_focused) {
        m_focused->m_focused = true;
        m_focused->onFocusChanged(true);
    }
    requestFrame();
}

void Host::collectFocusable(Widget* widget, std::vector<Widget*>& out) const {
    if (!widget->visible() || !widget->enabled()) {
        return;
    }
    if (widget->canFocus() && (widget->tabStop() || widget == m_focused)) {
        out.push_back(widget);
    }
    for (const auto& child : widget->children()) {
        collectFocusable(child.get(), out);
    }
}

void Host::focusNext(bool reverse) {
    if (!m_root) {
        return;
    }
    std::vector<Widget*> order;
    collectFocusable(inputRoot(), order);
    if (order.empty()) {
        return;
    }
    auto it = std::ranges::find(order, m_focused);
    std::size_t index = 0;
    if (it == order.end()) {
        index = reverse ? order.size() - 1 : 0;
    } else {
        const auto current = static_cast<std::size_t>(it - order.begin());
        index = reverse ? (current + order.size() - 1) % order.size() : (current + 1) % order.size();
    }
    setFocus(order[index], /*visible=*/true);
}

} // namespace wl::ui
