#include "ui/widgets/EmptyState.h"

#include <cmath>

namespace wl::ui {

namespace {
constexpr float kIconSize = 24.0f;
constexpr float kIconGap = 16.0f;
constexpr float kTextGap = 4.0f;
constexpr float kActionGap = 12.0f;
constexpr float kTextLine = 16.0f;
} // namespace

EmptyState::EmptyState(icons::Icon icon, std::wstring title, std::wstring body)
    : m_icon(icon), m_title(std::move(title)), m_body(std::move(body)) {
    setHitTestVisible(false);
}

Button& EmptyState::setAction(std::wstring label) {
    if (m_action) {
        m_action->setText(std::move(label));
        m_action->setVisible(true);
    } else {
        m_action = &add<Button>(ButtonKind::Secondary, std::move(label));
    }
    layout();
    return *m_action;
}

void EmptyState::clearAction() {
    if (m_action) {
        m_action->setVisible(false);
        m_action->onInvoke = nullptr;
    }
}

void EmptyState::setContent(icons::Icon icon, std::wstring title, std::wstring body) {
    m_icon = icon;
    m_title = std::move(title);
    m_body = std::move(body);
    layout();
    invalidate();
}

void EmptyState::hideAction() {
    if (m_action) {
        m_action->setVisible(false);
        layout();
    }
}

float EmptyState::contentTop() const {
    float height = kIconSize + kIconGap + kTextLine + kTextGap + kTextLine;
    if (m_action && m_action->visible()) {
        height += kActionGap + tokens::size::control;
    }
    return bounds().y + std::round((bounds().height - height) / 2);
}

void EmptyState::layout() {
    if (!m_action) {
        return;
    }
    const float top = contentTop() + kIconSize + kIconGap + kTextLine + kTextGap + kTextLine + kActionGap;
    const SizeF size = m_action->measure({});
    const RectF b = bounds();
    m_action->setBounds({b.x + std::round((b.width - size.width) / 2), top, size.width, size.height});
}

void EmptyState::paint(Canvas& canvas) {
    const RectF b = bounds();
    float y = contentTop();
    canvas.drawIcon(m_icon, {b.x + std::round((b.width - kIconSize) / 2), y}, tokens::Color::TextTertiary,
                    IconVariant::Regular24);
    y += kIconSize + kIconGap;
    canvas.drawText(m_title, {b.x, y, b.width, kTextLine}, tokens::TypeStyle::BodyStrong, tokens::Color::TextPrimary,
                    TextAlign::Center);
    y += kTextLine + kTextGap;
    canvas.drawText(m_body, {b.x, y, b.width, kTextLine}, tokens::TypeStyle::Caption, tokens::Color::TextSecondary,
                    TextAlign::Center);
}

} // namespace wl::ui
