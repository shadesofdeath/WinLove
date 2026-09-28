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
} // namespace

PageView::PageView(std::wstring title, std::wstring description)
    : m_title(std::move(title)), m_description(std::move(description)) {
    setAccessible(ui::AccessRole::Group, m_title);
}

float PageView::headerHeight() const {
    return kPadding + kTitleLine + (m_description.empty() ? 0.0f : kDescGap + kDescLine) + kPadding;
}

void PageView::layout() {
    if (!m_body) {
        return;
    }
    const RectF b = bounds();
    const float top = b.y + headerHeight();
    m_body->setBounds({b.x + kPadding, top, b.width - 2 * kPadding, std::max(b.bottom() - top - kPadding, 0.0f)});
}

void PageView::paint(ui::Canvas& canvas) {
    const RectF b = bounds();
    canvas.fillRect(b, Color::BgBase);
    const float x = b.x + kPadding;
    const float width = b.width - 2 * kPadding;
    canvas.drawText(m_title, {x, b.y + kPadding, width, kTitleLine}, TypeStyle::Title, Color::TextPrimary);
    if (!m_description.empty()) {
        canvas.drawText(m_description, {x, b.y + kPadding + kTitleLine + kDescGap, width, kDescLine}, TypeStyle::Caption,
                        Color::TextSecondary);
    }
}

} // namespace wl::app
