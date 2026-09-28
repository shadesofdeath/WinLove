#pragma once
// Single line of text in one type style and color token; ellipsis when it does not fit, and then
// the full text becomes the tooltip (contextmenu.md: "kesilmiş her metin tooltip alır").
#include "ui/widget/Widget.h"

#include <string>

namespace wl::ui {

class Label : public Widget {
public:
    Label(std::wstring text, tokens::TypeStyle style = tokens::TypeStyle::Body,
          tokens::Color color = tokens::Color::TextPrimary, TextAlign align = TextAlign::Leading);

    void setText(std::wstring text);
    [[nodiscard]] const std::wstring& text() const noexcept { return m_text; }
    void setColor(tokens::Color color);

    [[nodiscard]] SizeF measure(SizeF available) override;
    void layout() override;
    void paint(Canvas& canvas) override;

private:
    std::wstring m_text;
    tokens::TypeStyle m_style;
    tokens::Color m_color;
    TextAlign m_align;
    float m_textWidth = -1; // cached measurement
};

} // namespace wl::ui
