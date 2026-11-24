#include "ui/widget/Widget.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

// How far outside its rectangle a disabled widget may still draw and be dimmed with the rest (a
// text box's outline, a card's shadow line). Not a design size: slack of the dimming layer.
constexpr float kDimMargin = 8.0f;

void extend(RectF& all, RectF r) {
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    if (all.width <= 0 || all.height <= 0) {
        all = r;
        return;
    }
    const float x = std::min(all.x, r.x);
    const float y = std::min(all.y, r.y);
    all = {x, y, std::max(all.right(), r.right()) - x, std::max(all.bottom(), r.bottom()) - y};
}

} // namespace

RectF Widget::paintExtent() const {
    RectF all{};
    extend(all, m_bounds);
    for (const auto& child : m_children) {
        if (child->m_visible) {
            extend(all, child->paintExtent());
        }
    }
    return all;
}

Widget::~Widget() {
    if (m_host) {
        m_host->forget(this);
    }
}

Host* Widget::host() const noexcept {
    return m_host;
}

void Widget::addChild(std::unique_ptr<Widget> child) {
    child->m_parent = this;
    child->setHostRecursive(m_host);
    m_children.push_back(std::move(child));
    invalidate();
}

void Widget::clearChildren() {
    m_children.clear(); // destructors unregister from the host
    invalidate();
}

void Widget::removeChild(Widget* child) {
    std::erase_if(m_children, [child](const std::unique_ptr<Widget>& c) { return c.get() == child; });
    invalidate();
}

void Widget::bringToFront(Widget* child) {
    const auto it = std::ranges::find_if(m_children, [child](const std::unique_ptr<Widget>& c) { return c.get() == child; });
    if (it != m_children.end() && it + 1 != m_children.end()) {
        std::rotate(it, it + 1, m_children.end());
        invalidate();
    }
}

void Widget::setHostRecursive(Host* host) {
    m_host = host;
    if (m_host && std::exchange(m_animationPending, false)) {
        m_host->startAnimating(this); // e.g. Toggle::setOn() in a page constructor
    }
    for (auto& child : m_children) {
        child->setHostRecursive(host);
    }
}

void Widget::setBounds(RectF bounds) {
    m_bounds = bounds;
    layout();
}

SizeF Widget::measure(SizeF /*available*/) {
    return {m_bounds.width, m_bounds.height};
}

void Widget::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        if (!visible && m_host) {
            m_host->release(this);
        }
        invalidate();
    }
}

void Widget::setEnabled(bool enabled) {
    if (m_enabled != enabled) {
        m_enabled = enabled;
        if (!enabled && m_host) {
            m_host->release(this);
        }
        invalidate();
    }
}

bool Widget::enabled() const noexcept {
    for (const Widget* w = this; w; w = w->m_parent) {
        if (!w->m_enabled) {
            return false;
        }
    }
    return true;
}

void Widget::forceVisualState(bool hover, bool press) noexcept {
    m_forcedHover = hover;
    m_forcedPress = press;
}

void Widget::invalidate() {
    if (m_host) {
        m_host->requestFrame();
    }
}

void Widget::setTooltip(std::wstring text) {
    if (text == m_tooltip) {
        return;
    }
    m_tooltip = std::move(text);
    if (m_host && m_hovered) {
        m_host->tooltipChanged(this); // a widget with a tooltip per item (2026-11-10)
    }
}

float Widget::textWidth(std::wstring_view text, tokens::TypeStyle style, float fallback) const {
    return m_host ? std::ceil(m_host->text().measure(text, style)) : fallback;
}

bool Widget::activateOnKey(const KeyEvent& key, bool enterToo) {
    if (key.virtualKey == VK_SPACE || (enterToo && key.virtualKey == VK_RETURN)) {
        onClick();
        return true;
    }
    return false;
}

void Widget::animate() {
    if (m_host) {
        m_host->startAnimating(this);
    } else {
        m_animationPending = true;
    }
}

void Widget::paintTree(Canvas& canvas) {
    if (!m_visible) {
        return;
    }
    const bool dimmed = !m_enabled; // disabled subtree: opacity 0.45 (button.md)
    if (dimmed) {
        // A layer the size of what is dimmed, not of the window (audit UI-04). A widget without a
        // rectangle of its own (it draws somewhere else) keeps the unbounded layer.
        const RectF extent = paintExtent();
        if (extent.width > 0 && extent.height > 0) {
            canvas.pushOpacity(tokens::opacity::disabled, extent.inset(-kDimMargin, -kDimMargin));
        } else {
            canvas.pushOpacity(tokens::opacity::disabled);
        }
    }
    const float own = paintOpacity();
    if (own < 1.0f) {
        canvas.pushOpacity(own);
    }
    paint(canvas);
    if (!m_children.empty()) {
        const bool clip = clipsChildren();
        if (clip) {
            canvas.pushClip(m_bounds);
        }
        // Under a clip, a child that lies wholly outside it draws nothing that shows: a long form
        // scrolled by one row painted every control of every row (audit UI-02 / D4). The margin
        // covers what a control draws just outside its rectangle.
        const RectF view = m_bounds.inset(-kDimMargin, -kDimMargin);
        for (auto& child : m_children) {
            if (clip && child->m_visible) {
                const RectF extent = child->paintExtent();
                if (extent.width > 0 && extent.height > 0 &&
                    (extent.right() <= view.x || extent.x >= view.right() || extent.bottom() <= view.y ||
                     extent.y >= view.bottom())) {
                    continue;
                }
            }
            child->paintTree(canvas);
        }
        if (clip) {
            canvas.popClip();
        }
    }
    paintOverlay(canvas);
    if (own < 1.0f) {
        canvas.popOpacity();
    }
    if (dimmed) {
        canvas.popOpacity();
    }
}

Widget* Widget::hitTest(PointF p) {
    if (!m_visible || !m_bounds.contains(p)) {
        return nullptr;
    }
    // Topmost child wins: children painted later are on top.
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if (Widget* hit = (*it)->hitTest(p)) {
            return hit;
        }
    }
    return m_hitTestVisible ? this : nullptr;
}

} // namespace wl::ui
