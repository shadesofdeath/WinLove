#pragma once
// Theme color lookup over the generated token tables. D2D conversion lives in the render layer.
#include "ui/generated/Tokens.g.h"

#include <cstdint>

namespace wl::ui {

enum class ThemeKind : std::uint8_t { Dark, Light, HighContrast };

// Accent colors of the app settings (screen 19: swatches Bakır · Deniz · Nar · Gök · Zeytin).
// Copper is the token set of the handoff; the others replace the accent tokens (base, hover,
// pressed, subtle, focus, text on accent) in the dark and light themes — high contrast keeps its
// own. Each set meets the same contrast rules as the tokens (tests/ui/TokensTests.cpp).
enum class Accent : std::uint8_t { Copper, Sea, Pomegranate, Sky, Olive };
inline constexpr int kAccentCount = 5;
void setAccent(Accent accent) noexcept; // process-wide, UI thread; repaint afterwards
[[nodiscard]] Accent accent() noexcept;

struct Rgba {
    float r;
    float g;
    float b;
    float a;
};

[[nodiscard]] std::uint32_t colorArgb(ThemeKind theme, tokens::Color color) noexcept;
[[nodiscard]] Rgba toRgba(std::uint32_t argb) noexcept;
// The swatch of an accent (its dark-theme base, as screen 19 shows them in every theme).
[[nodiscard]] Rgba accentSwatch(Accent accent) noexcept;

inline Rgba color(ThemeKind theme, tokens::Color token) noexcept {
    return toRgba(colorArgb(theme, token));
}

// WCAG 2 contrast ratio (1..21).
[[nodiscard]] double contrastRatio(Rgba a, Rgba b) noexcept;

// The candidate that reads best on `background` (e.g. glyph color on the red close button).
[[nodiscard]] tokens::Color bestContrast(ThemeKind theme, tokens::Color background, tokens::Color first,
                                         tokens::Color second) noexcept;

} // namespace wl::ui
