#pragma once
// Button per the WinLoveUI package (D-107, Painter::Button): primary (accent fill, white text),
// secondary (panel fill + line border, raised on hover), subtle (no fill, hover overlay), ghost
// (accent text, hover overlay), danger; optional icon, icon-only (tooltip required). Height
// size::control (36), radius 10, 80 ms hover and press color transitions.
#include "ui/anim/Tween.h"
#include "ui/widget/Widget.h"

#include <functional>
#include <optional>
#include <string>

namespace wl::ui {

// Ribbon (D-109): the reference tool's ribbon button — no fill at rest, the icon in the accent color, the
// label in text.primary, a hover overlay; 32 high.
enum class ButtonKind : std::uint8_t { Primary, Secondary, Subtle, Danger, Ghost, Ribbon };

class Button : public Widget {
public:
    Button(ButtonKind kind, std::wstring text, std::optional<icons::Icon> icon = std::nullopt);
    // Icon-only button (24×24 by default); `tooltip` is mandatory by design.
    static std::unique_ptr<Button> iconOnly(icons::Icon icon, std::wstring tooltip, ButtonKind kind = ButtonKind::Subtle);

    std::function<void()> onInvoke;

    void setText(std::wstring text);

    [[nodiscard]] SizeF measure(SizeF available) override;
    void paint(Canvas& canvas) override;
    [[nodiscard]] float focusRadius() const override { return tokens::radius::button; }
    // A fixed height other than size::control (the side panel's 40 / 44 buttons).
    void setHeight(float height) { m_height = height; }

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
    float m_textWidth = -1;
    float m_height = 0; // 0: size::control
    Tween m_hover;
    Tween m_press;
};

} // namespace wl::ui
