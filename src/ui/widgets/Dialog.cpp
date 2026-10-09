#include "ui/widgets/Dialog.h"

#include "ui/widget/Host.h"

#include <algorithm>
#include <cmath>

namespace wl::ui {

namespace {
constexpr float kPadding = 16.0f;
constexpr float kTitleLine = 16.0f;
constexpr float kTitleGap = 12.0f;
constexpr float kButtonsGap = 24.0f;
constexpr float kButtonGap = 4.0f;
constexpr float kIconGap = 8.0f;
constexpr float kWindowMargin = 24.0f; // least space kept between the box and the window edge
constexpr float kMinContent = 48.0f;
} // namespace

Dialog::Dialog(std::wstring title, std::wstring body, std::optional<icons::Icon> icon, tokens::Color iconColor,
               float width)
    : m_title(std::move(title)), m_body(std::move(body)), m_icon(icon), m_iconColor(iconColor), m_width(width) {
    setAccessible(AccessRole::Group, m_title);
}

Button& Dialog::addButton(ButtonKind kind, std::wstring label, std::function<void()> onInvoke, bool primary) {
    Button& button = add<Button>(kind, std::move(label));
    button.onInvoke = std::move(onInvoke);
    m_buttons.push_back(&button);
    if (primary) {
        m_primary = &button;
    }
    layout();
    return button;
}

void Dialog::layout() {
    const RectF b = bounds();
    const float contentWidth = m_width - 2 * kPadding;
    if (host()) {
        m_bodyFull = std::ceil(host()->text().measureWrapped(m_body, tokens::TypeStyle::Body, contentWidth));
        m_bodyHeight = m_bodyFull;
        if (b.height > 0) {
            // A long body (an error with paths, DISM's text) never pushes the buttons off the window.
            const float others = kPadding + kTitleLine + kTitleGap + kButtonsGap + tokens::size::control + kPadding +
                                 (m_content ? kTitleGap + kMinContent : 0.0f);
            m_bodyHeight = std::max(std::min(m_bodyFull, std::floor(b.height - 2 * kWindowMargin - others)), 40.0f);
        }
        m_bodyOffset = std::clamp(m_bodyOffset, 0.0f, std::max(m_bodyFull - m_bodyHeight, 0.0f));
    }
    const float chrome = kPadding + kTitleLine + kTitleGap + m_bodyHeight + kButtonsGap + tokens::size::control + kPadding;
    // The box never outgrows the window: tall content (a long list) gets what is left and
    // scrolls inside itself, so the title and the buttons stay reachable.
    float contentHeight = m_contentHeight;
    if (m_content && b.height > 0) {
        const float room = b.height - 2 * kWindowMargin - chrome - kTitleGap;
        contentHeight = std::max(std::min(contentHeight, std::floor(room)), kMinContent);
    }
    const float content = m_content ? kTitleGap + contentHeight : 0.0f;
    const float height = chrome + content;
    m_box = {b.x + std::round((b.width - m_width) / 2), b.y + std::round((b.height - height) / 2), m_width, height};
    if (m_content) {
        m_content->setBounds({m_box.x + kPadding, m_box.y + kPadding + kTitleLine + kTitleGap + m_bodyHeight + kTitleGap,
                              contentWidth, contentHeight});
    }

    // Buttons right-aligned on the last row.
    float right = m_box.right() - kPadding;
    const float y = m_box.bottom() - kPadding - tokens::size::control;
    for (auto it = m_buttons.rbegin(); it != m_buttons.rend(); ++it) {
        const SizeF size = (*it)->measure({});
        (*it)->setBounds({right - size.width, y, size.width, size.height});
        right -= size.width + kButtonGap;
    }
}

void Dialog::paint(Canvas& canvas) {
    const float radius = tokens::radius::r3;
    canvas.panel(m_box, radius, tokens::elevation::dialog);

    float x = m_box.x + kPadding;
    const float y = m_box.y + kPadding;
    if (m_icon) {
        canvas.drawIcon(*m_icon, {x, y}, m_iconColor);
        x += tokens::size::icon + kIconGap;
    }
    canvas.drawText(m_title, {x, y, m_box.right() - kPadding - x, kTitleLine}, tokens::TypeStyle::BodyStrong,
                    tokens::Color::TextPrimary);
    const RectF body{m_box.x + kPadding, y + kTitleLine + kTitleGap, m_width - 2 * kPadding, m_bodyHeight};
    canvas.pushClip(body);
    canvas.drawTextWrapped(m_body, {body.x, body.y - m_bodyOffset, body.width, m_bodyFull}, tokens::TypeStyle::Body,
                           tokens::Color::TextSecondary);
    canvas.popClip();
    if (m_bodyFull > m_bodyHeight) {
        // Where in the text the view is: a thin bar at the body's right edge.
        const float track = body.height;
        const float thumb = std::max(track * m_bodyHeight / m_bodyFull, 16.0f);
        const float top = body.y + (track - thumb) * (m_bodyOffset / std::max(m_bodyFull - m_bodyHeight, 1.0f));
        canvas.fillRoundRect({body.right() + 6, top, 3, thumb}, 1.5f, tokens::Color::LineStrong);
    }
}

bool Dialog::onWheel(PointF /*p*/, float lines) {
    if (m_bodyFull <= m_bodyHeight) {
        return false;
    }
    m_bodyOffset = std::clamp(m_bodyOffset - lines * 18.0f, 0.0f, m_bodyFull - m_bodyHeight);
    invalidate();
    return true;
}

bool Dialog::onKeyDown(const KeyEvent& key) {
    if (key.virtualKey == VK_ESCAPE) {
        if (auto cancel = onCancel) { // copy: cancelling pops (destroys) this dialog
            cancel();
        }
        return true;
    }
    // A disabled primary (e.g. "Go" before the risk acknowledgement is ticked) must not be
    // reachable through Enter either.
    if (key.virtualKey == VK_RETURN && m_primary && m_primary->enabled()) {
        m_primary->onClick();
        return true;
    }
    return false;
}

void Dialog::onPointerUp(PointF p) {
    m_lastUp = p;
}

void Dialog::onClick() {
    // Clicks reach the dialog itself only on its backdrop or empty box area.
    if (!m_box.contains(m_lastUp)) {
        if (auto cancel = onCancel) {
            cancel();
        }
    }
}

} // namespace wl::ui
