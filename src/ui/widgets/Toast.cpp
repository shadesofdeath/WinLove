#include "ui/widgets/Toast.h"

#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
constexpr float kPaddingX = 12.0f;
constexpr float kIconGap = 12.0f;

} // namespace

Toast::Toast() {
    setVisible(false);
    setAccessible(AccessRole::Text, L"");
}

void Toast::show(InfoKind kind, std::wstring title, std::wstring message, std::wstring action,
                 std::function<void()> onAction) {
    m_kind = kind;
    m_title = std::move(title);
    m_message = std::move(message);
    m_action = std::move(action);
    m_onAction = std::move(onAction);
    setAccessible(AccessRole::Text, m_title);
    setVisible(true);
    m_enter.snapTo(0.0f);
    if (m_enter.animateTo(1.0f, tokens::motion::enterMs, tokens::motion::decelerate)) {
        animate();
    }
    invalidate();
}

bool Toast::tick(double now) {
    const bool running = m_enter.tick(now);
    invalidate();
    return running;
}

RectF Toast::actionRect() const {
    if (m_action.empty()) {
        return {};
    }
    const RectF b = bounds();
    const float w = textWidth(m_action, tokens::TypeStyle::BodyStrong, 48.0f) + 16.0f;
    return {b.right() - kPaddingX - w, b.y + (b.height - tokens::size::control) / 2, w, tokens::size::control};
}

void Toast::onPointerMove(PointF p) {
    const bool over = !m_action.empty() && actionRect().contains(p);
    if (over != m_overAction) {
        m_overAction = over;
        invalidate();
    }
}

void Toast::onClick() {
    if (m_overAction && m_onAction) {
        auto action = std::move(m_onAction); // the action may show another toast
        hide();
        action();
    }
}

void Toast::hide() {
    setVisible(false);
}

void Toast::onHoverChanged(bool /*hovered*/) {
    invalidate();
}

void Toast::paint(Canvas& canvas) {
    // Rises 8px into place as it fades in (paintOpacity).
    RectF b = bounds();
    const float rise = std::round((1.0f - m_enter.value()) * 8.0f);
    b.y += rise;
    const float radius = tokens::radius::r3;
    canvas.panel(b, radius, tokens::elevation::toast);
    const InfoStyle style = infoStyle(m_kind);
    canvas.fillRoundRect({b.x + 1, b.y + 8, 3, b.height - 16}, 1.5f, style.ink);
    const float x = b.x + kPaddingX + 2;
    canvas.drawIcon(style.icon, {x, b.y + std::round((b.height - tokens::size::icon) / 2)}, style.ink);
    const float textX = x + tokens::size::icon + kIconGap;
    const RectF action = actionRect();
    const float width = (m_action.empty() ? b.right() - kPaddingX : action.x - 8) - textX;
    if (!m_action.empty()) {
        RectF a = action;
        a.y += rise;
        if (m_overAction) {
            canvas.fillRoundRect(a, tokens::radius::r2, Color::BgRaised);
        }
        canvas.drawText(m_action, a, tokens::TypeStyle::BodyStrong, Color::AccentBase, TextAlign::Center);
    }
    canvas.drawText(m_title, {textX, b.y + 8, width, 16}, tokens::TypeStyle::BodyStrong, Color::TextPrimary);
    canvas.drawText(m_message, {textX, b.y + 24, width, 16}, tokens::TypeStyle::Caption, Color::TextSecondary);
}

} // namespace wl::ui
