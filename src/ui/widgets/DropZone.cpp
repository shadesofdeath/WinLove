#include "ui/widgets/DropZone.h"

#include <cmath>

namespace wl::ui {

namespace {
using tokens::Color;
constexpr float kIcon = 24.0f;
constexpr float kIconGap = 16.0f;
constexpr float kLine = 16.0f;
constexpr float kLineGap = 2.0f;
} // namespace

DropZone::DropZone(std::wstring title, std::wstring hint, std::wstring unsupported, std::wstring loading)
    : m_title(std::move(title)), m_hint(std::move(hint)), m_unsupported(std::move(unsupported)),
      m_loading(std::move(loading)) {
    setFocusable(true);
    setAccessible(AccessRole::Button, m_title);
}

void DropZone::setDragState(DragState state) {
    if (m_drag != state) {
        m_drag = state;
        invalidate();
    }
}

void DropZone::setLoading(bool loading) {
    m_isLoading = loading;
    invalidate();
}

void DropZone::onHoverChanged(bool hovered) {
    if (m_hover.animateTo(hovered ? 1.0f : 0.0f, tokens::motion::fastMs)) {
        animate();
    }
    invalidate();
}

bool DropZone::tick(double now) {
    const bool running = m_hover.tick(now);
    invalidate();
    return running;
}

void DropZone::onClick() {
    if (!m_isLoading && onInvoke) {
        onInvoke();
    }
}

bool DropZone::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_SPACE || key.virtualKey == VK_RETURN) {
        onClick();
        return true;
    }
    return false;
}

void DropZone::paint(Canvas& canvas) {
    const RectF b = bounds();
    const float radius = tokens::radius::r2;
    const float h = hovered() && !m_hover.running() ? 1.0f : m_hover.value();

    Ink border = Ink(Color::LineStrong, Color::TextTertiary, h); // hover: like the palette trigger
    Ink icon = Color::TextTertiary;
    Ink title = Color::TextPrimary;
    std::wstring_view titleText = m_isLoading ? m_loading : m_title;
    std::wstring_view hintText = m_hint;
    if (m_drag == DragState::Valid) {
        canvas.fillRoundRect(b, radius, Color::AccentSubtle);
        border = Color::AccentBase;
        icon = Color::AccentBase;
    } else if (m_drag == DragState::Invalid) {
        border = Color::StatusError;
        icon = Color::StatusError;
        title = Color::StatusError;
        titleText = m_unsupported;
    }
    canvas.strokeRoundRect(b, radius, border);

    const float content = kIcon + kIconGap + kLine + kLineGap + kLine;
    float y = b.y + std::round((b.height - content) / 2);
    canvas.drawIcon(m_drag == DragState::Invalid ? icons::Icon::ErrorOctagon : icons::Icon::Download,
                    {b.x + std::round((b.width - kIcon) / 2), y}, icon, IconVariant::Regular24);
    y += kIcon + kIconGap;
    canvas.drawText(titleText, {b.x + 8, y, b.width - 16, kLine}, tokens::TypeStyle::BodyStrong, title, TextAlign::Center);
    y += kLine + kLineGap;
    canvas.drawText(hintText, {b.x + 8, y, b.width - 16, kLine}, tokens::TypeStyle::Caption, Color::TextSecondary,
                    TextAlign::Center);
}

} // namespace wl::ui
