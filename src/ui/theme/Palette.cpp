#include "ui/theme/Palette.h"

#include <windows.h>

#include <array>
#include <cmath>

namespace wl::ui {

namespace {

// base, hover, pressed, subtle, text on accent — dark theme, then light theme. Copper is the
// handoff's own (never read from here). Derived from the screen 19 swatches: hover +12 % white,
// pressed −9 % (dark) / −10, −20 % (light), subtle 16–18 % over the page, the light base darkened
// until it reaches AA on the page and under white text.
struct AccentSet {
    std::uint32_t base, hover, pressed, subtle, onAccent;
};
constexpr AccentSet kAccentDark[kAccentCount] = {
    {0xFFD4905Au, 0xFFDE9F6Cu, 0xFFC27F4Bu, 0xFF3A2D22u, 0xFF1A1210u}, // Bakır (tokens)
    {0xFF46B8AEu, 0xFF5CC1B8u, 0xFF40A79Eu, 0xFF213230u, 0xFF071211u}, // Deniz
    {0xFFE2607Fu, 0xFFE5738Eu, 0xFFCE5774u, 0xFF3A2428u, 0xFF170A0Du}, // Nar
    {0xFF7FA7D9u, 0xFF8EB2DEu, 0xFF7498C5u, 0xFF2A3037u, 0xFF0D1116u}, // Gök
    {0xFF9CB86Au, 0xFFA8C17Cu, 0xFF8EA760u, 0xFF2F3225u, 0xFF10120Bu}, // Zeytin
};
constexpr AccentSet kAccentLight[kAccentCount] = {
    {0xFFA85C24u, 0xFF96511Fu, 0xFF84461Au, 0xFFF1DCC9u, 0xFFFFFFFFu},
    {0xFF2D7871u, 0xFF286C66u, 0xFF24605Au, 0xFFD6E8E2u, 0xFFFFFFFFu},
    {0xFFB04B63u, 0xFF9E4459u, 0xFF8D3C4Fu, 0xFFF2D8DAu, 0xFFFFFFFFu},
    {0xFF546E8Fu, 0xFF4C6381u, 0xFF435872u, 0xFFE0E5EAu, 0xFFFFFFFFu},
    {0xFF617242u, 0xFF57673Bu, 0xFF4E5B35u, 0xFFE5E8D6u, 0xFFFFFFFFu},
};

Accent g_accent = Accent::Copper;

// D-091: the dark theme's surfaces near black (user: "siyah tonlamasına yakın"), the layers further
// apart than the handoff's (#1A1918 base / #201E1C panel): page, panel, row hover and lines each a
// clear step. Text, accents and states keep the tokens' values (their contrast only grows).
struct Surface {
    tokens::Color color;
    std::uint32_t argb;
};
constexpr Surface kDarkSurfaces[] = {
    {tokens::Color::BgBase, 0xFF111010u},     {tokens::Color::BgPanel, 0xFF181716u},
    {tokens::Color::BgRaised, 0xFF242220u},   {tokens::Color::BgPressed, 0xFF2D2A27u},
    {tokens::Color::BgOverlay, 0xFF1C1B19u},  {tokens::Color::BgInput, 0xFF0B0A0Au},
    {tokens::Color::LineSubtle, 0xFF282624u}, {tokens::Color::LineStrong, 0xFF3C3834u},
};

// Windows' high contrast colors, when it is on (refreshSystemContrast).
bool g_systemContrast = false;
std::array<std::uint32_t, tokens::kColorCount> g_contrast{};

std::uint32_t sysColor(int index) noexcept {
    const COLORREF c = GetSysColor(index);
    return 0xFF000000u | (static_cast<std::uint32_t>(GetRValue(c)) << 16) | (static_cast<std::uint32_t>(GetGValue(c)) << 8) |
           GetBValue(c);
}

} // namespace

void setAccent(Accent value) noexcept {
    g_accent = static_cast<int>(value) < kAccentCount ? value : Accent::Copper;
}

Accent accent() noexcept {
    return g_accent;
}

Rgba accentSwatch(Accent value) noexcept {
    const int index = static_cast<int>(value) < kAccentCount ? static_cast<int>(value) : 0;
    return toRgba(kAccentDark[index].base);
}

std::uint32_t colorArgb(ThemeKind theme, tokens::Color color) noexcept {
    const auto index = static_cast<std::size_t>(color);
    if (index >= tokens::kColorCount) {
        return 0xFFFF00FFu; // loud magenta: an invalid token must be visible, not silent
    }
    if (g_accent != Accent::Copper && theme != ThemeKind::HighContrast) {
        const AccentSet& set = (theme == ThemeKind::Light ? kAccentLight : kAccentDark)[static_cast<int>(g_accent)];
        switch (color) {
        case tokens::Color::AccentBase:
        case tokens::Color::AccentFocus: return set.base;
        case tokens::Color::AccentHover: return set.hover;
        case tokens::Color::AccentPressed: return set.pressed;
        case tokens::Color::AccentSubtle: return set.subtle;
        case tokens::Color::TextOnAccent: return set.onAccent;
        default: break;
        }
    }
    if (theme == ThemeKind::HighContrast && g_systemContrast) {
        return g_contrast[index];
    }
    if (theme == ThemeKind::Dark) {
        for (const auto& surface : kDarkSurfaces) {
            if (surface.color == color) {
                return surface.argb;
            }
        }
    }
    switch (theme) {
    case ThemeKind::Dark: return tokens::kDark[index];
    case ThemeKind::Light: return tokens::kLight[index];
    case ThemeKind::HighContrast: return tokens::kHighContrast[index];
    }
    return tokens::kDark[index];
}

bool refreshSystemContrast() noexcept {
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW), 0, nullptr};
    g_systemContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                       (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    if (!g_systemContrast) {
        return false;
    }
    using C = tokens::Color;
    g_contrast = tokens::kHighContrast; // status colors, scrim and shadows stay the tokens'
    const std::uint32_t window = sysColor(COLOR_WINDOW);
    const std::uint32_t text = sysColor(COLOR_WINDOWTEXT);
    const std::uint32_t highlight = sysColor(COLOR_HIGHLIGHT);
    const std::uint32_t onHighlight = sysColor(COLOR_HIGHLIGHTTEXT);
    const std::uint32_t gray = sysColor(COLOR_GRAYTEXT);
    const std::uint32_t hot = sysColor(COLOR_HOTLIGHT);
    for (const C c : {C::BgBase, C::BgPanel, C::BgRaised, C::BgOverlay, C::BgInput, C::AccentSubtle, C::StatusSuccessSubtle,
                      C::StatusWarningSubtle, C::StatusErrorSubtle, C::StatusInfoSubtle}) {
        g_contrast[static_cast<std::size_t>(c)] = window;
    }
    for (const C c : {C::TextPrimary, C::TextSecondary, C::TextTertiary, C::LineSubtle, C::LineStrong}) {
        g_contrast[static_cast<std::size_t>(c)] = text;
    }
    for (const C c : {C::AccentBase, C::AccentHover, C::AccentPressed, C::BgPressed}) {
        g_contrast[static_cast<std::size_t>(c)] = highlight;
    }
    g_contrast[static_cast<std::size_t>(C::AccentFocus)] = hot;
    g_contrast[static_cast<std::size_t>(C::TextOnAccent)] = onHighlight;
    g_contrast[static_cast<std::size_t>(C::TextDisabled)] = gray;
    return true;
}

bool systemHighContrast() noexcept {
    return g_systemContrast;
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
