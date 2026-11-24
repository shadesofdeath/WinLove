#pragma once
// Theme color lookup over the generated token tables. D2D conversion lives in the render layer.
#include "ui/generated/Tokens.g.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wl::ui {

enum class ThemeKind : std::uint8_t { Dark, Light, HighContrast, Black };

// D-143: the accent is any colour the user picks (Ayarlar › Vurgu rengi). From it the accent tokens — base,
// hover, pressed, subtle, focus, accent text, text on accent — are worked out for the dark, light and pure
// black themes (deriveAccent; the rules are in Palette.cpp); high contrast keeps its own. The five colours
// of D-137 are presets: Blue (Mavi, #3070D9) is the tokens' own set and the default; Copper, Sea,
// Pomegranate and Olive keep D-137's tables (tests/ui/AccentTests.cpp compares them with the rules).
enum class Accent : std::uint8_t { Blue, Copper, Sea, Pomegranate, Olive };
inline constexpr int kAccentCount = 5;
inline constexpr std::uint32_t kDefaultAccent = 0x3070D9u; // 0xRRGGBB: Mavi (D-137)
[[nodiscard]] std::uint32_t presetColor(Accent accent) noexcept; // 0xRRGGBB
[[nodiscard]] std::optional<Accent> presetOf(std::uint32_t rgb) noexcept;

// Process-wide, UI thread; repaint afterwards. 0xRRGGBB (the top byte is ignored).
void setAccentColor(std::uint32_t rgb) noexcept;
[[nodiscard]] std::uint32_t accentColor() noexcept;
inline void setAccent(Accent accent) noexcept {
    setAccentColor(presetColor(accent));
}

// One theme's accent tokens, 0xAARRGGBB.
struct AccentSet {
    std::uint32_t base = 0;
    std::uint32_t hover = 0;
    std::uint32_t pressed = 0;
    std::uint32_t subtle = 0;
    std::uint32_t onAccent = 0;
    std::uint32_t text = 0;
    std::uint32_t focus = 0;
    [[nodiscard]] bool operator==(const AccentSet&) const = default;
};
struct AccentDerivation {
    AccentSet dark;
    AccentSet light;
    AccentSet black;
    bool darkened = false;  // the fill is darker than the chosen colour: white text on it must read
    bool lightened = false; // dark themes: the fill is lighter, to show as a mark on the dark card
};
// The D-143 rules for any colour (no preset table involved).
[[nodiscard]] AccentDerivation deriveAccent(std::uint32_t rgb) noexcept;
// What the app draws with: a preset's D-137 table, any other colour deriveAccent's.
[[nodiscard]] AccentDerivation accentTokens(std::uint32_t rgb) noexcept;

// "#RRGGBB", "RRGGBB" or "#RGB", any case, spaces around allowed; nullopt for anything else.
[[nodiscard]] std::optional<std::uint32_t> parseHexColor(std::wstring_view text) noexcept;
[[nodiscard]] std::wstring hexColor(std::uint32_t rgb); // "#RRGGBB"

// The colour picker's coordinates: hue 0..360, saturation and value 0..1.
struct Hsv {
    float h = 0;
    float s = 0;
    float v = 0;
};
[[nodiscard]] Hsv toHsv(std::uint32_t rgb) noexcept;
[[nodiscard]] std::uint32_t fromHsv(Hsv hsv) noexcept;

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
// An opaque 0xRRGGBB colour (a swatch, the picker's colours).
[[nodiscard]] inline Rgba rgbColor(std::uint32_t rgb) noexcept {
    return toRgba(0xFF000000u | (rgb & 0xFFFFFFu));
}
// The swatch of a preset (its base, as screen 19 shows them in every theme).
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
