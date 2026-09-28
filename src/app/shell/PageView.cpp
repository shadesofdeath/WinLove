#include "app/shell/PageView.h"

#include <algorithm>

namespace wl::app {

using ui::RectF;
using ui::tokens::Color;
using ui::tokens::TypeStyle;

namespace {
constexpr float kPadding = 16.0f;
constexpr float kTitleLine = 22.0f;
constexpr float kDescGap = 2.0f;
constexpr float kDescLine = 16.0f;
constexpr float kActionTop = 17.0f; // design: header buttons at y 49 = content top 32 + 17
constexpr float kActionGap = 4.0f;
} // namespace

PageView::PageView(std::wstring title, std::wstring description)
    : m_title(std::move(title)), m_description(std::move(description)) {
    setAccessible(ui::AccessRole::Group, m_title);
}

ui::Button& PageView::addAction(ui::ButtonKind kind, std::wstring label, std::optional<ui::icons::Icon> icon) {
    ui::Button& button = add<ui::Button>(kind, std::move(label), icon);
    m_actions.push_back(&button);
    layout();
    return button;
}

float PageView::headerHeight() const {
    return kPadding + kTitleLine + (m_description.empty() ? 0.0f : kDescGap + kDescLine);
}

void PageView::setHeader(std::wstring title, std::wstring description) {
    if (title == m_title && description == m_description) {
        return;
    }
    m_title = std::move(title);
    m_description = std::move(description);
    layout();
    invalidate();
}

void PageView::layout() {
    const RectF b = bounds();
    float right = b.right() - kPadding;
    for (auto it = m_actions.rbegin(); it != m_actions.rend(); ++it) {
        const ui::SizeF size = (*it)->measure({});
        (*it)->setBounds({right - size.width, b.y + kActionTop, size.width, size.height});
        right -= size.width + kActionGap;
    }
    if (m_body) {
        const float top = b.y + headerHeight();
        m_body->setBounds({b.x + kPadding, top, b.width - 2 * kPadding, std::max(b.bottom() - top - kPadding, 0.0f)});
    }
}

void PageView::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgBase);
    const float x = b.x + kPadding;
    const float textRight = m_actions.empty() ? b.right() - kPadding : m_actions.front()->bounds().x - kPadding;
    const float width = std::max(textRight - x, 0.0f);
    canvas.drawText(m_title, {x, b.y + kPadding, width, kTitleLine}, TypeStyle::Title, Color::TextPrimary);
    if (!m_description.empty()) {
        canvas.drawText(m_description, {x, b.y + kPadding + kTitleLine + kDescGap, width, kDescLine}, TypeStyle::Caption,
                        Color::TextSecondary);
    }
}

} // namespace wl::app
