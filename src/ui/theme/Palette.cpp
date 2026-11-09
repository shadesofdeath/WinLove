#include "ui/theme/Palette.h"

#include <windows.h>

#include <array>
#include <cmath>

namespace wl::ui {

namespace {

// base, hover, pressed, subtle, text on accent, accent text — dark theme, then light theme. Blue is
// the tokens' own (never read from here; D-109: the reference tool's, which follow the Windows accent). D-107: one fill per accent in both themes (white text
// at >= 4.6:1), hover +8 % white, pressed -10 %, subtle 16 % over the dark panel / 8 % over white,
// accent text lightened (dark) or darkened (light) until it reads at >= 5:1.
struct AccentSet {
    std::uint32_t base, hover, pressed, subtle, onAccent, text;
};
constexpr AccentSet kAccentDark[kAccentCount] = {
    {0xFF4CC2FFu, 0xFF47B1E8u, 0xFF42A1D2u, 0xFF3E3E3Eu, 0xFF000000u, 0xFF4CC2FFu}, // Mavi (tokens, D-109)
    {0xFFAF5F29u, 0xFFB56C3Au, 0xFF9E5625u, 0xFF30231Du, 0xFFFFFFFFu, 0xFFC3875Fu}, // Bakır
    {0xFF147971u, 0xFF26847Du, 0xFF126D66u, 0xFF172728u, 0xFFFFFFFFu, 0xFF59A09Bu}, // Deniz
    {0xFFC13C5Cu, 0xFFC64B69u, 0xFFAE3653u, 0xFF331C24u, 0xFFFFFFFFu, 0xFFD4798Fu}, // Nar
    {0xFF5F7A2Eu, 0xFF6C853Fu, 0xFF566E29u, 0xFF23281Eu, 0xFFFFFFFFu, 0xFF879B63u}, // Zeytin
};
constexpr AccentSet kAccentLight[kAccentCount] = {
    {0xFF0067C0u, 0xFF1975C5u, 0xFF3183CAu, 0xFFE3E3E3u, 0xFFFFFFFFu, 0xFF0058CDu},
    {0xFFAF5F29u, 0xFFB56C3Au, 0xFF9E5625u, 0xFFF9F2EEu, 0xFFFFFFFFu, 0xFFA15726u},
    {0xFF0F766Eu, 0xFF22817Au, 0xFF0E6A63u, 0xFFECF4F3u, 0xFFFFFFFFu, 0xFF0F766Eu},
    {0xFFBE3455u, 0xFFC34463u, 0xFFAB2F4Cu, 0xFFFAEFF1u, 0xFFFFFFFFu, 0xFFBE3455u},
    {0xFF5F7A2Eu, 0xFF6C853Fu, 0xFF566E29u, 0xFFF2F4EEu, 0xFFFFFFFFu, 0xFF59732Bu},
};

Accent g_accent = Accent::Blue;

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
    g_accent = static_cast<int>(value) < kAccentCount ? value : Accent::Blue;
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
    if (g_accent != Accent::Blue && theme != ThemeKind::HighContrast) {
        const AccentSet& set = (theme == ThemeKind::Light ? kAccentLight : kAccentDark)[static_cast<int>(g_accent)];
        switch (color) {
        case tokens::Color::AccentBase:
        case tokens::Color::AccentFocus: return set.base;
        case tokens::Color::AccentHover: return set.hover;
        case tokens::Color::AccentPressed: return set.pressed;
        case tokens::Color::AccentSubtle: return set.subtle;
        case tokens::Color::TextOnAccent: return set.onAccent;
        case tokens::Color::AccentText: return set.text;
        default: break;
        }
    }
    if (theme == ThemeKind::HighContrast && g_systemContrast) {
        return g_contrast[index];
    }
    switch (theme) {
    case ThemeKind::Dark: return tokens::kDark[index];
    case ThemeKind::Light: return tokens::kLight[index];
    case ThemeKind::HighContrast: return tokens::kHighContrast[index];
    case ThemeKind::Black: return tokens::kBlack[index];
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
    for (const C c : {C::BgBase, C::BgPanel, C::BgRaised, C::BgOverlay, C::BgInput, C::BgHover, C::AccentSubtle, C::StatusSuccessSubtle,
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
    g_contrast[static_cast<std::size_t>(C::AccentText)] = hot;
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
