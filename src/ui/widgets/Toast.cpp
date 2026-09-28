#include "ui/widgets/Toast.h"

#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
constexpr float kPaddingX = 12.0f;
constexpr float kIconGap = 12.0f;

Color inkFor(InfoKind kind) {
    switch (kind) {
    case InfoKind::Success: return Color::StatusSuccess;
    case InfoKind::Warning: return Color::StatusWarning;
    case InfoKind::Error: return Color::StatusError;
    case InfoKind::Info: break;
    }
    return Color::StatusInfo;
}

icons::Icon iconFor(InfoKind kind) {
    switch (kind) {
    case InfoKind::Success: return icons::Icon::SuccessCircle;
    case InfoKind::Warning: return icons::Icon::WarningTriangle;
    case InfoKind::Error: return icons::Icon::ErrorOctagon;
    case InfoKind::Info: break;
    }
    return icons::Icon::InfoCircle;
}
} // namespace

Toast::Toast() {
    setVisible(false);
    setAccessible(AccessRole::Text, L"");
}

void Toast::show(InfoKind kind, std::wstring title, std::wstring message) {
    m_kind = kind;
    m_title = std::move(title);
    m_message = std::move(message);
    setAccessible(AccessRole::Text, m_title);
    setVisible(true);
    invalidate();
}

void Toast::hide() {
    setVisible(false);
}

void Toast::onHoverChanged(bool hovered) {
    m_hovered = hovered;
}

void Toast::paint(Canvas& canvas) {
    const RectF b = bounds();
    const float radius = tokens::radius::r3;
    canvas.dropShadow(b, radius, tokens::elevation::toast);
    canvas.fillRoundRect(b, radius, Color::BgOverlay);
    canvas.strokeRoundRect(b, radius, Color::LineStrong);
    const float x = b.x + kPaddingX;
    canvas.drawIcon(iconFor(m_kind), {x, b.y + std::round((b.height - tokens::size::icon) / 2)}, inkFor(m_kind));
    const float textX = x + tokens::size::icon + kIconGap;
    const float width = b.right() - kPaddingX - textX;
    canvas.drawText(m_title, {textX, b.y + 8, width, 16}, tokens::TypeStyle::BodyStrong, Color::TextPrimary);
    canvas.drawText(m_message, {textX, b.y + 24, width, 16}, tokens::TypeStyle::Caption, Color::TextSecondary);
}

} // namespace wl::ui
