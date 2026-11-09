#pragma once
// D-114: WinLove's Fluent-style colour icons. An icon drawn in the accent ink (navigation, ribbon,
// empty states, menus) takes its own colour from a small semantic palette (folders amber, removal
// red, security green …) and, for shapes with an inside, a soft fill of the same colour under the
// stroke: two tones, "slightly colourful" rather than all accent blue. Icons in any other ink
// (text colours, disabled, on an accent background) stay one colour.
#include "ui/generated/Icons.g.h"
#include "ui/generated/Tokens.g.h"

namespace wl::ui {

struct IconTone {
    tokens::Color color = tokens::Color::IconSlate;
    bool fill = false; // a soft fill under the stroke (closed shapes: folders, files, shields …)
};

[[nodiscard]] IconTone iconTone(icons::Icon icon) noexcept;

// The fill's opacity under the stroke.
inline constexpr float kIconFillOpacity = 0.2f;

} // namespace wl::ui
