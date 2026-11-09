#pragma once
// Theme color lookup over the generated token tables. D2D conversion lives in the render layer.
#include "ui/generated/Tokens.g.h"

#include <cstdint>

namespace wl::ui {

enum class ThemeKind : std::uint8_t { Dark, Light, HighContrast, Black };

// Accent colors of the app settings. D-107: Blue is the token set of the design package
// (design/tokens.json, #2563EB); the others replace the accent tokens (base, hover, pressed, subtle,
// focus, accent text, text on accent) in the dark and light themes — high contrast keeps its own.
// As in the package, the fill is the same in both themes and carries white text; accent text and
// the subtle fill differ per theme. Each set meets the contrast rules of the tokens
// (tests/ui/TokensTests.cpp).
enum class Accent : std::uint8_t { Blue, Copper, Sea, Pomegranate, Olive };
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

// High contrast follows Windows' own scheme (Settings › Accessibility › Contrast themes): when
// Windows has it on, the HC theme takes its colors from GetSysColor — a light "Desert" scheme
// included — instead of the fixed token set (audit D9). Read again on WM_SETTINGCHANGE.
// Returns whether Windows has high contrast on.
bool refreshSystemContrast() noexcept;
[[nodiscard]] bool systemHighContrast() noexcept;
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
