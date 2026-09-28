#pragma once
// Button per 03_components/button.md: primary / secondary / subtle / danger, optional icon,
// icon-only (tooltip required), heights 20/24/28, 80 ms hover and press color transitions.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <optional>
#include <string>

namespace wl::ui {

enum class ButtonKind : std::uint8_t { Primary, Secondary, Subtle, Danger };

class Button : public Widget {
public:
    Button(ButtonKind kind, std::wstring text, std::optional<icons::Icon> icon = std::nullopt);
    // Icon-only button (24×24 by default); `tooltip` is mandatory by design.
    static std::unique_ptr<Button> iconOnly(icons::Icon icon, std::wstring tooltip, ButtonKind kind = ButtonKind::Subtle);

    std::function<void()> onInvoke;

    void setText(std::wstring text);
    void setHeight(float height) noexcept { m_height = height; }

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] float focusRadius() const override { return tokens::radius::r2; }

    void onHoverChanged(bool hovered) override;
    void onPressedChanged(bool pressed) override;
    void onClick() override;
    bool onKeyDown(const KeyEvent& key) override;
    bool tick(double now) override;

private:
    [[nodiscard]] bool isIconOnly() const noexcept { return m_text.empty() && m_icon.has_value(); }

    ButtonKind m_kind;
    std::wstring m_text;
    std::optional<icons::Icon> m_icon;
    float m_height = tokens::size::control;
    float m_textWidth = -1;
    Tween m_hover;
    Tween m_press;
};

} // namespace wl::ui
