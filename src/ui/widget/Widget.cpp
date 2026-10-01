#include "ui/widget/Widget.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

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
        canvas.pushOpacity(tokens::opacity::disabled);
    }
    paint(canvas);
    if (!m_children.empty()) {
        const bool clip = clipsChildren();
        if (clip) {
            canvas.pushClip(m_bounds);
        }
        for (auto& child : m_children) {
            child->paintTree(canvas);
        }
        if (clip) {
            canvas.popClip();
        }
    }
    paintOverlay(canvas);
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
