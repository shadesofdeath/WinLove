#include "ui/widgets/Toast.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
constexpr float kPaddingX = 14.0f; // D-107 / D-108
constexpr float kIconGap = 12.0f;
constexpr float kTop = 10.0f;     // above the title
constexpr float kBottom = 10.0f;  // below the message
constexpr float kLineGap = 2.0f;  // between title and message
constexpr float kTitleLine = 18.0f;
constexpr float kMessageLine = 16.0f;

} // namespace

float Toast::preferredHeight(float width) const {
    if (!host()) {
        return kHeight;
    }
    const float textX = 2 + kPaddingX + tokens::size::icon + kIconGap;
    const float actionW = m_action.empty() ? 0.0f : textWidth(m_action, tokens::TypeStyle::BodyStrong, 48.0f) + 24.0f + 8.0f;
    const float textW = std::max(width - textX - kPaddingX - actionW, 40.0f);
    const auto& text = host()->text();
    const float title = std::max(kTitleLine, std::ceil(text.measureWrapped(m_title, tokens::TypeStyle::BodyStrong, textW)));
    const float message =
        m_message.empty() ? 0.0f : std::max(kMessageLine, std::ceil(text.measureWrapped(m_message, tokens::TypeStyle::Caption, textW)));
    return std::clamp(kTop + title + (message > 0 ? kLineGap + message : 0.0f) + kBottom, kHeight, kMaxHeight);
}

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
    // Grow (or shrink) upwards to fit the new text: the bottom edge stays where the owner put it.
    if (const RectF b = bounds(); b.width > 0) {
        const float height = preferredHeight(b.width);
        setBounds({b.x, b.bottom() - height, b.width, height});
    }
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
    const float w = textWidth(m_action, tokens::TypeStyle::BodyStrong, 48.0f) + 24.0f;
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
    canvas.fillRoundRect({b.x + 1, b.y + 10, 2, b.height - 20}, 1.0f, style.ink);
    const float x = b.x + kPaddingX + 2;
    // One title line and one message line: the icon in the middle; more: beside the title.
    const bool tall = b.height > kHeight + 1;
    const float iconY = tall ? b.y + kTop + std::round((kTitleLine - tokens::size::icon) / 2)
                             : b.y + std::round((b.height - tokens::size::icon) / 2);
    canvas.drawIcon(style.icon, {x, iconY}, style.ink);
    const float textX = x + tokens::size::icon + kIconGap;
    const RectF action = actionRect();
    const float width = (m_action.empty() ? b.right() - kPaddingX : action.x - 8) - textX;
    if (!m_action.empty()) {
        RectF a = action;
        a.y += rise;
        if (m_overAction) {
            canvas.fillRoundRect(a, tokens::radius::button, Color::BgHover);
        }
        canvas.drawText(m_action, a, tokens::TypeStyle::BodyStrong, Color::AccentText, TextAlign::Center);
    }
    // Both wrap: nothing ends in "…" (the user, 2026-11-09).
    const auto& text = canvas.text();
    const float titleH = std::max(kTitleLine, std::ceil(text.measureWrapped(m_title, tokens::TypeStyle::BodyStrong, width)));
    canvas.drawTextWrapped(m_title, {textX, b.y + kTop, width, titleH}, tokens::TypeStyle::BodyStrong, Color::TextPrimary);
    if (!m_message.empty()) {
        const float messageY = b.y + kTop + titleH + kLineGap;
        canvas.drawTextWrapped(m_message, {textX, messageY, width, std::max(b.bottom() - kBottom - messageY + 2, kMessageLine)},
                               tokens::TypeStyle::Caption, Color::TextSecondary);
    }
}

} // namespace wl::ui
