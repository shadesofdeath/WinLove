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
// D-136: the lines of the title (BodyStrong) and the message (Caption), from their type styles.
constexpr float kTitleLine = tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::BodyStrong)].lineHeight;
constexpr float kMessageLine = tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::Caption)].lineHeight;
constexpr float kActionRowGap = 6.0f; // D-145: the text and the row of two actions

} // namespace

float Toast::preferredHeight(float width) const {
    if (!host()) {
        return kHeight;
    }
    const float textX = 2 + kPaddingX + tokens::size::icon + kIconGap;
    const float actionW =
        m_action.empty() || stacked() ? 0.0f : textWidth(m_action, tokens::TypeStyle::BodyStrong, 48.0f) + 24.0f + 8.0f;
    const float textW = std::max(width - textX - kPaddingX - actionW, 40.0f);
    const auto& text = host()->text();
    const float title = std::max(kTitleLine, std::ceil(text.measureWrapped(m_title, tokens::TypeStyle::BodyStrong, textW)));
    const float message =
        m_message.empty() ? 0.0f : std::max(kMessageLine, std::ceil(text.measureWrapped(m_message, tokens::TypeStyle::Caption, textW)));
    const float actions = stacked() ? kActionRowGap + tokens::size::control : 0.0f;
    return std::clamp(kTop + title + (message > 0 ? kLineGap + message : 0.0f) + actions + kBottom, kHeight, kMaxHeight);
}

Toast::Toast() {
    setVisible(false);
    setAccessible(AccessRole::Text, L"");
}

void Toast::show(InfoKind kind, std::wstring title, std::wstring message, std::wstring action,
                 std::function<void()> onAction) {
    show(kind, std::move(title), std::move(message), std::move(action), std::move(onAction), {}, {});
}

void Toast::show(InfoKind kind, std::wstring title, std::wstring message, std::wstring action, std::function<void()> onAction,
                 std::wstring second, std::function<void()> onSecond) {
    m_second = std::move(second);
    m_onSecond = std::move(onSecond);
    m_overSecond = false;
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
    if (stacked()) {
        return {b.right() - kPaddingX - w, b.bottom() - kBottom - tokens::size::control, w, tokens::size::control};
    }
    return {b.right() - kPaddingX - w, b.y + (b.height - tokens::size::control) / 2, w, tokens::size::control};
}

RectF Toast::secondRect() const {
    if (!stacked()) {
        return {};
    }
    const RectF first = actionRect();
    const float w = textWidth(m_second, tokens::TypeStyle::BodyStrong, 48.0f) + 24.0f;
    return {first.x - 4 - w, first.y, w, first.height};
}

void Toast::onPointerMove(PointF p) {
    const bool over = !m_action.empty() && actionRect().contains(p);
    const bool overSecond = stacked() && secondRect().contains(p);
    if (over != m_overAction || overSecond != m_overSecond) {
        m_overAction = over;
        m_overSecond = overSecond;
        invalidate();
    }
}

void Toast::onClick() {
    if (m_overAction && m_onAction) {
        auto action = std::move(m_onAction); // the action may show another toast
        hide();
        action();
    } else if (m_overSecond && m_onSecond) {
        auto action = std::move(m_onSecond);
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
    const float width = (m_action.empty() || stacked() ? b.right() - kPaddingX : action.x - 8) - textX;
    if (!m_action.empty() && stacked()) {
        // D-145: the first action is the toast's main one (filled), the second beside it, plain.
        RectF a = action;
        a.y += rise;
        canvas.fillRoundRect(a, tokens::radius::button, m_overAction ? Color::AccentHover : Color::AccentBase);
        canvas.drawText(m_action, a, tokens::TypeStyle::BodyStrong, Color::TextOnAccent, TextAlign::Center);
        RectF second = secondRect();
        second.y += rise;
        if (m_overSecond) {
            canvas.fillRoundRect(second, tokens::radius::button, Color::BgHover);
        }
        canvas.drawText(m_second, second, tokens::TypeStyle::BodyStrong, Color::AccentText, TextAlign::Center);
    } else if (!m_action.empty()) {
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
    // A title alone in the least height sits in the middle, level with the icon (it hung at the top).
    const float titleY = m_message.empty() && !tall ? b.y + std::round((b.height - titleH) / 2) : b.y + kTop;
    canvas.drawTextWrapped(m_title, {textX, titleY, width, titleH}, tokens::TypeStyle::BodyStrong, Color::TextPrimary);
    if (!m_message.empty()) {
        const float messageY = b.y + kTop + titleH + kLineGap;
        canvas.drawTextWrapped(m_message, {textX, messageY, width, std::max(b.bottom() - kBottom - messageY + 2, kMessageLine)},
                               tokens::TypeStyle::Caption, Color::TextSecondary);
    }
}

} // namespace wl::ui
