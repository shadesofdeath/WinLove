#pragma once
// Theme color lookup over the generated token tables. D2D conversion lives in the render layer.
#include "ui/generated/Tokens.g.h"

#include <cstdint>

namespace wl::ui {

enum class ThemeKind : std::uint8_t { Dark, Light, HighContrast };

struct Rgba {
    float r;
    float g;
    float b;
    float a;
};

[[nodiscard]] std::uint32_t colorArgb(ThemeKind theme, tokens::Color color) noexcept;
[[nodiscard]] Rgba toRgba(std::uint32_t argb) noexcept;

inline Rgba color(ThemeKind theme, tokens::Color token) noexcept {
    return toRgba(colorArgb(theme, token));
}

// WCAG 2 contrast ratio (1..21).
[[nodiscard]] double contrastRatio(Rgba a, Rgba b) noexcept;

// The candidate that reads best on `background` (e.g. glyph color on the red close button).
[[nodiscard]] tokens::Color bestContrast(ThemeKind theme, tokens::Color background, tokens::Color first,
                                         tokens::Color second) noexcept;

} // namespace wl::ui
