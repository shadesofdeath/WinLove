#include "ui/widgets/InfoBar.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {

using tokens::Color;

constexpr float kPaddingLeft = 16.0f;
constexpr float kGap = 8.0f;

} // namespace

InfoStyle infoStyle(InfoKind kind) noexcept {
    switch (kind) {
    case InfoKind::Success: return {Color::StatusSuccessSubtle, Color::StatusSuccess, icons::Icon::SuccessCircle};
    case InfoKind::Warning: return {Color::StatusWarningSubtle, Color::StatusWarning, icons::Icon::WarningTriangle};
    case InfoKind::Error: return {Color::StatusErrorSubtle, Color::StatusError, icons::Icon::ErrorOctagon};
    case InfoKind::Info: break;
    }
    return {Color::StatusInfoSubtle, Color::StatusInfo, icons::Icon::InfoCircle};
}

InfoBar::InfoBar(InfoKind kind, std::wstring title, std::wstring message, std::wstring closeTooltip)
    : m_kind(kind), m_title(std::move(title)), m_message(std::move(message)) {
    auto close = Button::iconOnly(icons::Icon::Close, std::move(closeTooltip));
    m_close = close.get();
    m_close->onInvoke = [this] {
        if (auto close = onClose) { // copy: closing may destroy this bar
            close();
        }
    };
    addChild(std::move(close));
    setAccessible(AccessRole::Group, m_title);
}

void InfoBar::set(InfoKind kind, std::wstring title, std::wstring message) {
    m_kind = kind;
    m_title = std::move(title);
    m_message = std::move(message);
    invalidate();
}

void InfoBar::setAction(std::wstring label, std::function<void()> onInvoke) {
    if (!m_action) {
        m_action = &add<Button>(ButtonKind::Subtle, L"");
    }
    const bool show = !label.empty() && onInvoke;
    m_action->setText(std::move(label));
    m_action->onInvoke = std::move(onInvoke);
    m_action->setVisible(show);
    layout();
}

void InfoBar::layout() {
    const RectF b = bounds();
    const float size = tokens::size::control;
    m_close->setBounds({b.right() - 4 - size, b.y + std::round((b.height - size) / 2), size, size});
    if (m_action && m_action->visible()) {
        const SizeF a = m_action->measure({});
        m_action->setBounds({m_close->bounds().x - 4 - a.width, b.y + std::round((b.height - a.height) / 2), a.width, a.height});
    }
}

void InfoBar::paint(Canvas& canvas) {
    const RectF b = bounds();
    const InfoStyle style = infoStyle(m_kind);
    canvas.fillRect(b, style.background);
    canvas.hairlineH(b.x, b.y, b.width, Color::LineSubtle);
    canvas.hairlineH(b.x, b.bottom() - 1.0f / canvas.scale(), b.width, Color::LineSubtle);
    float x = b.x + kPaddingLeft;
    canvas.drawIcon(style.icon, {x, b.y + std::round((b.height - tokens::size::icon) / 2)}, style.ink);
    x += tokens::size::icon + kGap;
    const float right = (m_action && m_action->visible() ? m_action->bounds().x : m_close->bounds().x) - kGap;
    const float titleWidth = std::min(std::ceil(canvas.text().measure(m_title, tokens::TypeStyle::BodyStrong)) + 1,
                                      std::max(right - x, 0.0f));
    canvas.drawText(m_title, {x, b.y, titleWidth, b.height}, tokens::TypeStyle::BodyStrong, Color::TextPrimary);
    x += titleWidth + 4;
    canvas.drawText(m_message, {x, b.y, std::max(right - x, 0.0f), b.height}, tokens::TypeStyle::Body,
                    Color::TextSecondary);
}

} // namespace wl::ui
