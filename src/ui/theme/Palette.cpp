#include "ui/theme/Palette.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <format>

namespace wl::ui {

namespace {

using C = tokens::Color;

// ---- the five presets (D-137) ----------------------------------------------------------------------
// base, hover, pressed, subtle, text on accent, accent text, focus — dark theme, light theme, and the pure
// black theme's selection fill. Blue (Mavi) is the tokens' own (listed so the five read as one family).
// Made by build\lab\identity\derive.py, the rules deriveAccent() below follows; Deniz's and Zeytin's dark
// readable tone were then softened by hand (deriveAccent gives #52AFA8 / #8DAA5E for #4FB1AA / #93AD5F:
// tests/ui/AccentTests.cpp). A preset keeps its table, so nothing changes for whoever chose one.
constexpr std::uint32_t kPresetRgb[kAccentCount] = {0x3070D9u, 0xAF5F29u, 0x177F77u, 0xC64262u, 0x5F7A2Eu};
constexpr AccentSet kAccentDark[kAccentCount] = {
    {0xFF3070D9u, 0xFF2C67C8u, 0xFF285EB6u, 0xFF27364Fu, 0xFFFFFFFFu, 0xFF74A3EAu, 0xFF74A3EAu}, // Mavi (tokens)
    {0xFFAF5F29u, 0xFFA15726u, 0xFF935022u, 0xFF41332Bu, 0xFFFFFFFFu, 0xFFD9905Eu, 0xFFD9905Eu}, // Bakır
    {0xFF177F77u, 0xFF15756Du, 0xFF136B64u, 0xFF22393Bu, 0xFFFFFFFFu, 0xFF4FB1AAu, 0xFF4FB1AAu}, // Deniz
    {0xFFC64262u, 0xFFB63D5Au, 0xFFA63752u, 0xFF452D37u, 0xFFFFFFFFu, 0xFFD88A98u, 0xFFD88A98u}, // Nar
    {0xFF5F7A2Eu, 0xFF57702Au, 0xFF506627u, 0xFF31382Cu, 0xFFFFFFFFu, 0xFF93AD5Fu, 0xFF93AD5Fu}, // Zeytin
};
constexpr AccentSet kAccentLight[kAccentCount] = {
    {0xFF3070D9u, 0xFF2C67C8u, 0xFF285EB6u, 0xFFEAF1FBu, 0xFFFFFFFFu, 0xFF2462C7u, 0xFF3070D9u},
    {0xFFAF5F29u, 0xFFA15726u, 0xFF935022u, 0xFFF7EFEAu, 0xFFFFFFFFu, 0xFF9B5524u, 0xFFAF5F29u},
    {0xFF177F77u, 0xFF15756Du, 0xFF136B64u, 0xFFE8F2F1u, 0xFFFFFFFFu, 0xFF14736Bu, 0xFF177F77u},
    {0xFFC64262u, 0xFFB63D5Au, 0xFFA63752u, 0xFFF9ECEFu, 0xFFFFFFFFu, 0xFFB73856u, 0xFFC64262u},
    {0xFF5F7A2Eu, 0xFF57702Au, 0xFF506627u, 0xFFEFF2EAu, 0xFFFFFFFFu, 0xFF566E2Bu, 0xFF5F7A2Eu},
};
// Pure black: the dark sets, with the selection fill mixed over the black panel.
constexpr std::uint32_t kAccentBlackSubtle[kAccentCount] = {0xFF182843u, 0xFF362419u, 0xFF122B2Cu, 0xFF3C1D27u, 0xFF232A1Au};

std::uint32_t g_accent = kDefaultAccent;
bool g_customAccent = false; // anything but Mavi: the accent tokens come from g_sets
AccentDerivation g_sets{};

// Windows' high contrast colors, when it is on (refreshSystemContrast).
bool g_systemContrast = false;
std::array<std::uint32_t, tokens::kColorCount> g_contrast{};

std::uint32_t sysColor(int index) noexcept {
    const COLORREF c = GetSysColor(index);
    return 0xFF000000u | (static_cast<std::uint32_t>(GetRValue(c)) << 16) | (static_cast<std::uint32_t>(GetGValue(c)) << 8) |
           GetBValue(c);
}

// ---- D-143: the rules ------------------------------------------------------------------------------
// Ported from build\lab\identity\derive.py (D-137) operation for operation — the same double arithmetic,
// Python's round-half-to-even, colorsys' HLS — so the presets come out as their tables:
// - fill (base): the chosen colour. White text must read on it (>= 4.6:1): a lighter colour is darkened —
//   OKLab lightness lowered, hue and chroma kept (chroma only as far as sRGB allows). Dark themes: the fill
//   is also a mark on the dark card (tab underline, switch, check box; >= 3:1 on bg.panel): a darker colour
//   is lightened the same way for the dark and pure black themes. (#4B0082 stays #4B0082 in the light theme
//   and becomes #8B55CC-ish in the dark ones.)
// - hover / pressed: the fill 8 % / 16 % toward black (white text only gains).
// - subtle (the selection fill): the fill over the card at 20 % (dark), 10 % (light), 24 % (pure black);
//   more, 2 % at a time, while it is within 6 levels of the hover fill (greys).
// - accent text and the focus ring of the dark themes: the fill lightened in HLS steps of 0.01 (light theme:
//   darkened) until it reads at >= 4.6:1 on the page, the frame, the card, a group, a menu, an input, a
//   hovered row and the selection fill. If that tone came out more colourful (OKLCH chroma) than the chosen
//   colour, its chroma is brought down to the colour's and the stepping goes on — what D-137 did by hand
//   for Deniz and Zeytin. The light theme's focus ring is the fill.
// - text on accent: white.
constexpr double kWhiteOnFill = 4.6;
constexpr double kFillMark = 3.0;
constexpr double kReadable = 4.6;
constexpr double kHoverDarken = 0.08;
constexpr double kPressedDarken = 0.16;
constexpr double kSubtleDark = 0.20;
constexpr double kSubtleLight = 0.10;
constexpr double kSubtleBlack = 0.24;
constexpr double kSubtleStep = 0.02;
constexpr double kSubtleMost = 0.5;
constexpr int kSelectionApart = 6; // levels between the selection and the hover fill
constexpr double kHlsStep = 0.01;
constexpr std::uint32_t kWhite = 0xFFFFFFu;

struct Rgb {
    double r;
    double g;
    double b;
};

int red(std::uint32_t c) noexcept {
    return static_cast<int>((c >> 16) & 0xFF);
}
int green(std::uint32_t c) noexcept {
    return static_cast<int>((c >> 8) & 0xFF);
}
int blue(std::uint32_t c) noexcept {
    return static_cast<int>(c & 0xFF);
}

// Python's int(round(v)): half to even (the default rounding mode of nearbyint).
int roundHalfEven(double v) noexcept {
    return std::clamp(static_cast<int>(std::nearbyint(v)), 0, 255);
}

std::uint32_t pack(double r, double g, double b) noexcept {
    return (static_cast<std::uint32_t>(roundHalfEven(r)) << 16) | (static_cast<std::uint32_t>(roundHalfEven(g)) << 8) |
           static_cast<std::uint32_t>(roundHalfEven(b));
}

// `k` of a over b (palette.py mix).
std::uint32_t mix(std::uint32_t a, std::uint32_t b, double k) noexcept {
    return pack(red(a) * k + red(b) * (1 - k), green(a) * k + green(b) * (1 - k), blue(a) * k + blue(b) * (1 - k));
}

std::uint32_t darken(std::uint32_t c, double k) noexcept {
    return mix(0, c, k);
}

// A translucent token (bg.hover, 0xAARRGGBB) over an opaque colour, unrounded.
Rgb over(std::uint32_t top, std::uint32_t bottom) noexcept {
    const double a = static_cast<double>((top >> 24) & 0xFF) / 255;
    return {red(top) * a + red(bottom) * (1 - a), green(top) * a + green(bottom) * (1 - a), blue(top) * a + blue(bottom) * (1 - a)};
}

double linear(double channel255) noexcept {
    const double v = channel255 / 255;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(Rgb c) noexcept {
    return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

Rgb rgbOf(std::uint32_t c) noexcept {
    return {static_cast<double>(red(c)), static_cast<double>(green(c)), static_cast<double>(blue(c))};
}

double ratio(Rgb a, Rgb b) noexcept {
    const double la = luminance(a) + 0.05;
    const double lb = luminance(b) + 0.05;
    return std::max(la, lb) / std::min(la, lb);
}

double ratio(std::uint32_t a, std::uint32_t b) noexcept {
    return ratio(rgbOf(a), rgbOf(b));
}

// Python's float % 1.0 (the result takes the divisor's sign).
double mod1(double x) noexcept {
    double m = std::fmod(x, 1.0);
    if (m != 0.0) {
        if (m < 0) {
            m += 1.0;
        }
    } else {
        m = 0.0;
    }
    return m;
}

// colorsys.rgb_to_hls / hls_to_rgb (Python 3.14), operation for operation.
struct Hls {
    double h;
    double l;
    double s;
};

Hls rgbToHls(double r, double g, double b) noexcept {
    const double maxc = std::max({r, g, b});
    const double minc = std::min({r, g, b});
    const double sumc = maxc + minc;
    const double rangec = maxc - minc;
    const double l = sumc / 2.0;
    if (minc == maxc) {
        return {0.0, l, 0.0};
    }
    const double s = l <= 0.5 ? rangec / sumc : rangec / (2.0 - maxc - minc);
    const double rc = (maxc - r) / rangec;
    const double gc = (maxc - g) / rangec;
    const double bc = (maxc - b) / rangec;
    double h = 0;
    if (r == maxc) {
        h = bc - gc;
    } else if (g == maxc) {
        h = 2.0 + rc - bc;
    } else {
        h = 4.0 + gc - rc;
    }
    return {mod1(h / 6.0), l, s};
}

double hlsValue(double m1, double m2, double hue) noexcept {
    constexpr double kOneSixth = 1.0 / 6.0;
    constexpr double kTwoThird = 2.0 / 3.0;
    hue = mod1(hue);
    if (hue < kOneSixth) {
        return m1 + (m2 - m1) * hue * 6.0;
    }
    if (hue < 0.5) {
        return m2;
    }
    if (hue < kTwoThird) {
        return m1 + (m2 - m1) * (kTwoThird - hue) * 6.0;
    }
    return m1;
}

Rgb hlsToRgb(double h, double l, double s) noexcept {
    if (s == 0.0) {
        return {l, l, l};
    }
    const double m2 = l <= 0.5 ? l * (1.0 + s) : l + s - (l * s);
    const double m1 = 2.0 * l - m2;
    constexpr double kOneThird = 1.0 / 3.0;
    return {hlsValue(m1, m2, h + kOneThird), hlsValue(m1, m2, h), hlsValue(m1, m2, h - kOneThird)};
}

// derive.py hls_shift: the HLS lightness moved by `dl`, rounded to bytes.
std::uint32_t hlsShift(std::uint32_t c, double dl) noexcept {
    const Hls hls = rgbToHls(red(c) / 255.0, green(c) / 255.0, blue(c) / 255.0);
    const double l = std::min(std::max(hls.l + dl, 0.0), 1.0);
    const Rgb out = hlsToRgb(hls.h, l, hls.s);
    return pack(out.r * 255, out.g * 255, out.b * 255);
}

// ---- OKLab (Björn Ottosson's matrices) ----
struct Lab {
    double l;
    double a;
    double b;
};

double toLinear(double v01) noexcept {
    return v01 <= 0.04045 ? v01 / 12.92 : std::pow((v01 + 0.055) / 1.055, 2.4);
}

double toGamma(double v) noexcept {
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
}

Lab toLab(std::uint32_t c) noexcept {
    const double r = toLinear(red(c) / 255.0);
    const double g = toLinear(green(c) / 255.0);
    const double b = toLinear(blue(c) / 255.0);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

// Linear sRGB of an OKLab colour (may lie outside 0..1).
Rgb labToLinear(Lab c) noexcept {
    const double l = std::pow(c.l + 0.3963377774 * c.a + 0.2158037573 * c.b, 3);
    const double m = std::pow(c.l - 0.1055613458 * c.a - 0.0638541728 * c.b, 3);
    const double s = std::pow(c.l - 0.0894841775 * c.a - 1.2914855480 * c.b, 3);
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}

bool inGamut(Rgb c) noexcept {
    constexpr double e = 1e-6;
    return c.r >= -e && c.r <= 1 + e && c.g >= -e && c.g <= 1 + e && c.b >= -e && c.b <= 1 + e;
}

double chroma(std::uint32_t c) noexcept {
    const Lab lab = toLab(c);
    return std::hypot(lab.a, lab.b);
}

// OKLCH → bytes; the chroma is lowered (hue and lightness kept) as far as sRGB needs.
std::uint32_t fromLch(double l, double chromaWanted, double hue) noexcept {
    const double ca = std::cos(hue);
    const double sa = std::sin(hue);
    double c = chromaWanted;
    if (!inGamut(labToLinear({l, c * ca, c * sa}))) {
        double lo = 0;
        double hi = c;
        for (int i = 0; i < 30; ++i) {
            const double mid = (lo + hi) / 2;
            (inGamut(labToLinear({l, mid * ca, mid * sa})) ? lo : hi) = mid;
        }
        c = lo;
    }
    const Rgb lin = labToLinear({l, c * ca, c * sa});
    return pack(toGamma(lin.r) * 255, toGamma(lin.g) * 255, toGamma(lin.b) * 255);
}

// The colour with its OKLCH chroma brought down to `most` (lightness and hue kept).
std::uint32_t capChroma(std::uint32_t c, double most) noexcept {
    const Lab lab = toLab(c);
    const double have = std::hypot(lab.a, lab.b);
    if (have <= most) {
        return c;
    }
    return fromLch(lab.l, most, std::atan2(lab.b, lab.a));
}

// The colour with its OKLab lightness moved (hue and chroma kept) to the nearest point where `ok` holds:
// down when `lighten` is false, up when it is true. `ok` must be monotonic along that way.
template <class Ok>
std::uint32_t fitLightness(std::uint32_t c, bool lighten, Ok ok) noexcept {
    const Lab lab = toLab(c);
    const double have = std::hypot(lab.a, lab.b);
    const double hue = std::atan2(lab.b, lab.a);
    double lo = lighten ? lab.l : 0.0; // lighten: hi holds; darken: lo holds
    double hi = lighten ? 1.0 : lab.l;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2;
        const bool good = ok(fromLch(mid, have, hue));
        if (lighten) {
            (good ? hi : lo) = mid;
        } else {
            (good ? lo : hi) = mid;
        }
    }
    return fromLch(lighten ? hi : lo, have, hue);
}

std::uint32_t token(const std::array<std::uint32_t, tokens::kColorCount>& theme, C c) noexcept {
    return theme[static_cast<std::size_t>(c)] & 0xFFFFFFu;
}

int largestStep(std::uint32_t a, Rgb b) noexcept {
    const double d = std::max({std::abs(red(a) - b.r), std::abs(green(a) - b.g), std::abs(blue(a) - b.b)});
    return static_cast<int>(std::lround(d));
}

double worstContrast(std::uint32_t ink, const std::uint32_t* surfaces, std::size_t count, Rgb hover) noexcept {
    double worst = ratio(rgbOf(ink), hover);
    for (std::size_t i = 0; i < count; ++i) {
        worst = std::min(worst, ratio(ink, surfaces[i]));
    }
    return worst;
}

// The selection fill of `base` over the theme's card, apart from its hover fill.
std::uint32_t selectionFill(std::uint32_t base, const std::array<std::uint32_t, tokens::kColorCount>& theme, double k) noexcept {
    const std::uint32_t panel = token(theme, C::BgPanel);
    const Rgb hover = over(theme[static_cast<std::size_t>(C::BgHover)], panel);
    std::uint32_t subtle = mix(base, panel, k);
    while (largestStep(subtle, hover) < kSelectionApart && k < kSubtleMost) {
        k += kSubtleStep;
        subtle = mix(base, panel, k);
    }
    return subtle;
}

// The readable tone of `base` on the theme's surfaces and its selection fill.
std::uint32_t readableTone(std::uint32_t base, std::uint32_t start, const std::array<std::uint32_t, tokens::kColorCount>& theme,
                           std::uint32_t subtle, bool light) noexcept {
    const std::uint32_t surfaces[] = {token(theme, C::BgBase),   token(theme, C::BgChrome), token(theme, C::BgTitleBar),
                                      token(theme, C::BgPanel),  token(theme, C::BgRaised), token(theme, C::BgOverlay),
                                      token(theme, C::BgInput),  subtle,                    token(theme, C::BgPressed)};
    const std::size_t count = light ? std::size(surfaces) : std::size(surfaces) - 1; // pressed: light only (derive.py)
    const Rgb hover = over(theme[static_cast<std::size_t>(C::BgHover)], token(theme, C::BgPanel));
    const double step = light ? -kHlsStep : kHlsStep;
    std::uint32_t text = start;
    for (int i = 0; i < 400 && worstContrast(text, surfaces, count, hover) < kReadable; ++i) {
        text = hlsShift(text, step);
    }
    const double most = chroma(base);
    if (chroma(text) > most) {
        text = capChroma(text, most);
        for (int i = 0; i < 400 && worstContrast(text, surfaces, count, hover) < kReadable; ++i) {
            text = capChroma(hlsShift(text, step), most);
        }
    }
    return text;
}

constexpr std::uint32_t opaque(std::uint32_t rgb) noexcept {
    return 0xFF000000u | (rgb & 0xFFFFFFu);
}

AccentSet makeSet(std::uint32_t base, std::uint32_t subtle, std::uint32_t text, std::uint32_t focus) noexcept {
    return {opaque(base),   opaque(darken(base, kHoverDarken)), opaque(darken(base, kPressedDarken)), opaque(subtle),
            opaque(kWhite), opaque(text),                       opaque(focus)};
}

} // namespace

std::uint32_t presetColor(Accent value) noexcept {
    const int index = static_cast<int>(value);
    return index >= 0 && index < kAccentCount ? kPresetRgb[index] : kDefaultAccent;
}

std::optional<Accent> presetOf(std::uint32_t rgb) noexcept {
    for (int i = 0; i < kAccentCount; ++i) {
        if (kPresetRgb[i] == (rgb & 0xFFFFFFu)) {
            return static_cast<Accent>(i);
        }
    }
    return std::nullopt;
}

AccentDerivation deriveAccent(std::uint32_t rgb) noexcept {
    rgb &= 0xFFFFFFu;
    AccentDerivation out;
    // The fill: white text reads on it (every theme), and in the dark themes it shows on the dark card.
    std::uint32_t lightBase = rgb;
    if (ratio(kWhite, rgb) < kWhiteOnFill) {
        lightBase = fitLightness(rgb, /*lighten=*/false, [](std::uint32_t c) { return ratio(kWhite, c) >= kWhiteOnFill; });
        out.darkened = lightBase != rgb;
    }
    const std::uint32_t darkPanel = token(tokens::kDark, C::BgPanel);
    std::uint32_t darkBase = lightBase;
    if (ratio(lightBase, darkPanel) < kFillMark) {
        const std::uint32_t lifted =
            fitLightness(lightBase, /*lighten=*/true, [darkPanel](std::uint32_t c) { return ratio(c, darkPanel) >= kFillMark; });
        if (ratio(kWhite, lifted) >= kWhiteOnFill) { // always so: the two bounds leave room between them
            darkBase = lifted;
            out.lightened = darkBase != lightBase;
        }
    }
    const std::uint32_t darkSubtle = selectionFill(darkBase, tokens::kDark, kSubtleDark);
    const std::uint32_t darkText = readableTone(darkBase, darkBase, tokens::kDark, darkSubtle, /*light=*/false);
    out.dark = makeSet(darkBase, darkSubtle, darkText, darkText);

    const std::uint32_t lightSubtle = selectionFill(lightBase, tokens::kLight, kSubtleLight);
    const std::uint32_t lightText = readableTone(lightBase, lightBase, tokens::kLight, lightSubtle, /*light=*/true);
    out.light = makeSet(lightBase, lightSubtle, lightText, lightBase);

    // Pure black: the dark set over the black card (its own selection fill); the readable tone is the
    // dark one unless that one does not read on the black surfaces (then it goes on from there).
    const std::uint32_t blackSubtle = selectionFill(darkBase, tokens::kBlack, kSubtleBlack);
    const std::uint32_t blackText = readableTone(darkBase, darkText, tokens::kBlack, blackSubtle, /*light=*/false);
    out.black = makeSet(darkBase, blackSubtle, blackText, blackText);
    return out;
}

AccentDerivation accentTokens(std::uint32_t rgb) noexcept {
    if (const auto preset = presetOf(rgb)) {
        const int i = static_cast<int>(*preset);
        AccentDerivation out;
        out.dark = kAccentDark[i];
        out.light = kAccentLight[i];
        out.black = kAccentDark[i];
        out.black.subtle = kAccentBlackSubtle[i];
        return out;
    }
    return deriveAccent(rgb);
}

void setAccentColor(std::uint32_t rgb) noexcept {
    rgb &= 0xFFFFFFu;
    if (rgb == g_accent && (g_customAccent || rgb == kDefaultAccent)) {
        return;
    }
    g_accent = rgb;
    g_customAccent = rgb != kDefaultAccent;
    if (g_customAccent) {
        g_sets = accentTokens(rgb);
    }
}

std::uint32_t accentColor() noexcept {
    return g_accent;
}

Rgba accentSwatch(Accent value) noexcept {
    return rgbColor(presetColor(value));
}

std::optional<std::uint32_t> parseHexColor(std::wstring_view text) noexcept {
    while (!text.empty() && std::iswspace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back())) {
        text.remove_suffix(1);
    }
    if (!text.empty() && text.front() == L'#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6 && text.size() != 3) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const wchar_t ch : text) {
        int digit = -1;
        if (ch >= L'0' && ch <= L'9') {
            digit = ch - L'0';
        } else if (ch >= L'a' && ch <= L'f') {
            digit = ch - L'a' + 10;
        } else if (ch >= L'A' && ch <= L'F') {
            digit = ch - L'A' + 10;
        }
        if (digit < 0) {
            return std::nullopt;
        }
        value = text.size() == 3 ? (value << 8) | static_cast<std::uint32_t>(digit * 17) : (value << 4) | static_cast<std::uint32_t>(digit);
    }
    return value;
}

std::wstring hexColor(std::uint32_t rgb) {
    return std::format(L"#{:06X}", rgb & 0xFFFFFFu);
}

Hsv toHsv(std::uint32_t rgb) noexcept {
    const float r = static_cast<float>(red(rgb)) / 255.0f;
    const float g = static_cast<float>(green(rgb)) / 255.0f;
    const float b = static_cast<float>(blue(rgb)) / 255.0f;
    const float maxc = std::max({r, g, b});
    const float minc = std::min({r, g, b});
    const float range = maxc - minc;
    Hsv out{0.0f, maxc > 0 ? range / maxc : 0.0f, maxc};
    if (range > 0) {
        float h = 0;
        if (maxc == r) {
            h = (g - b) / range;
        } else if (maxc == g) {
            h = 2.0f + (b - r) / range;
        } else {
            h = 4.0f + (r - g) / range;
        }
        out.h = std::fmod(h * 60.0f + 360.0f, 360.0f);
    }
    return out;
}

std::uint32_t fromHsv(Hsv hsv) noexcept {
    const float h = std::fmod(std::fmod(hsv.h, 360.0f) + 360.0f, 360.0f) / 60.0f;
    const float s = std::clamp(hsv.s, 0.0f, 1.0f);
    const float v = std::clamp(hsv.v, 0.0f, 1.0f);
    const float c = v * s;
    const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0;
    float g = 0;
    float b = 0;
    switch (static_cast<int>(h)) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
    }
    auto byte = [](float f) { return static_cast<std::uint32_t>(std::clamp(std::lround(f * 255.0f), 0L, 255L)); };
    return (byte(r + m) << 16) | (byte(g + m) << 8) | byte(b + m);
}

std::uint32_t colorArgb(ThemeKind theme, tokens::Color color) noexcept {
    const auto index = static_cast<std::size_t>(color);
    if (index >= tokens::kColorCount) {
        return 0xFFFF00FFu; // loud magenta: an invalid token must be visible, not silent
    }
    if (g_customAccent && theme != ThemeKind::HighContrast) {
        const AccentSet& set = theme == ThemeKind::Light ? g_sets.light : theme == ThemeKind::Black ? g_sets.black : g_sets.dark;
        switch (color) {
        case C::AccentBase: return set.base;
        case C::AccentFocus: return set.focus;
        case C::AccentHover: return set.hover;
        case C::AccentPressed: return set.pressed;
        case C::AccentSubtle: return set.subtle;
        case C::TextOnAccent: return set.onAccent;
        case C::AccentText: return set.text;
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
    g_contrast = tokens::kHighContrast; // status colors, scrim and shadows stay the tokens'
    const std::uint32_t window = sysColor(COLOR_WINDOW);
    const std::uint32_t text = sysColor(COLOR_WINDOWTEXT);
    const std::uint32_t highlight = sysColor(COLOR_HIGHLIGHT);
    const std::uint32_t onHighlight = sysColor(COLOR_HIGHLIGHTTEXT);
    const std::uint32_t gray = sysColor(COLOR_GRAYTEXT);
    const std::uint32_t hot = sysColor(COLOR_HOTLIGHT);
    for (const C c : {C::BgBase, C::BgPanel, C::BgRaised, C::BgOverlay, C::BgInput, C::BgControl, C::BgHover, C::BgChrome,
                      C::BgTitleBar, C::AccentSubtle, C::StatusSuccessSubtle, C::StatusWarningSubtle, C::StatusErrorSubtle,
                      C::StatusInfoSubtle}) {
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
