#include "ui/widgets/Label.h"

#include "ui/widget/Host.h"

#include <cmath>

namespace wl::ui {

Label::Label(std::wstring text, tokens::TypeStyle style, tokens::Color color, TextAlign align)
    : m_text(std::move(text)), m_style(style), m_color(color), m_align(align) {
    setHitTestVisible(false);
}

void Label::setText(std::wstring text) {
    if (text != m_text) {
        m_text = std::move(text);
        m_textWidth = -1;
        layout();
        invalidate();
    }
}

void Label::setColor(tokens::Color color) {
    m_color = color;
    invalidate();
}

SizeF Label::measure(SizeF /*available*/) {
    if (m_textWidth < 0 && host()) {
        m_textWidth = std::ceil(host()->text().measure(m_text, m_style));
    }
    return {std::max(m_textWidth, 0.0f), TextStyles::spec(m_style).lineHeight};
}

void Label::layout() {
    // Truncated text shows the full string as tooltip; hit-testable only then.
    const bool truncated = measure({}).width > bounds().width + 0.5f;
    setTooltip(truncated ? m_text : std::wstring{});
    setHitTestVisible(truncated);
}

void Label::paint(Canvas& canvas) {
    canvas.drawText(m_text, bounds(), m_style, m_color, m_align);
}

} // namespace wl::ui
