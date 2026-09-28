#include "ui/theme/Palette.h"

#include <cmath>

namespace wl::ui {

std::uint32_t colorArgb(ThemeKind theme, tokens::Color color) noexcept {
    const auto index = static_cast<std::size_t>(color);
    if (index >= tokens::kColorCount) {
        return 0xFFFF00FFu; // loud magenta: an invalid token must be visible, not silent
    }
    switch (theme) {
    case ThemeKind::Dark: return tokens::kDark[index];
    case ThemeKind::Light: return tokens::kLight[index];
    case ThemeKind::HighContrast: return tokens::kHighContrast[index];
    }
    return tokens::kDark[index];
}

Rgba toRgba(std::uint32_t argb) noexcept {
    constexpr float k = 1.0f / 255.0f;
    return Rgba{
        static_cast<float>((argb >> 16) & 0xFF) * k,
        static_cast<float>((argb >> 8) & 0xFF) * k,
        static_cast<float>(argb & 0xFF) * k,
        static_cast<float>((argb >> 24) & 0xFF) * k,
    };
}

namespace {

double linear(float channel) {
    return channel <= 0.03928 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double luminance(Rgba c) {
    return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

} // namespace

double contrastRatio(Rgba a, Rgba b) noexcept {
    const double la = luminance(a) + 0.05;
    const double lb = luminance(b) + 0.05;
    return la > lb ? la / lb : lb / la;
}

tokens::Color bestContrast(ThemeKind theme, tokens::Color background, tokens::Color first,
                           tokens::Color second) noexcept {
    const Rgba bg = color(theme, background);
    return contrastRatio(color(theme, first), bg) >= contrastRatio(color(theme, second), bg) ? first : second;
}

} // namespace wl::ui
