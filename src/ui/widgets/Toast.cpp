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

void Toast::onHoverChanged(bool /*hovered*/) {
    invalidate();
}

void Toast::paint(Canvas& canvas) {
    const RectF b = bounds();
    const float radius = tokens::radius::r3;
    canvas.panel(b, radius, tokens::elevation::toast);
    const float x = b.x + kPaddingX;
    const InfoStyle style = infoStyle(m_kind);
    canvas.drawIcon(style.icon, {x, b.y + std::round((b.height - tokens::size::icon) / 2)}, style.ink);
    const float textX = x + tokens::size::icon + kIconGap;
    const float width = b.right() - kPaddingX - textX;
    canvas.drawText(m_title, {textX, b.y + 8, width, 16}, tokens::TypeStyle::BodyStrong, Color::TextPrimary);
    canvas.drawText(m_message, {textX, b.y + 24, width, 16}, tokens::TypeStyle::Caption, Color::TextSecondary);
}

} // namespace wl::ui
