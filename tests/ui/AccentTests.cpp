// D-143: a free accent colour. The rules that make the accent tokens from any colour (ui/theme/Palette.cpp
// deriveAccent) give the D-137 tables for the five presets, and keep every contrast rule of
// tests/ui/PaletteTests.cpp for any colour — light yellow, white, black, greys, deep purple, teal and a
// sweep of hues and random colours; hex parsing and the picker's HSV.
#include "support/TestGraphics.h"
#include "ui/theme/Palette.h"
#include "ui/widget/Host.h"
#include "ui/widgets/ColorPicker.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace wl::ui;
using C = tokens::Color;

namespace {

constexpr ThemeKind kThemes[] = {ThemeKind::Dark, ThemeKind::Light, ThemeKind::Black};

Rgba over(Rgba top, Rgba bottom) {
    const float a = top.a;
    return {top.r * a + bottom.r * (1 - a), top.g * a + bottom.g * (1 - a), top.b * a + bottom.b * (1 - a), 1.0f};
}

int largestChannelStep(Rgba a, Rgba b) {
    return static_cast<int>(std::lround(255.0f * std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)})));
}

// OKLab, for "how far apart do two colours look" (the hand-tuned tones of D-137).
struct Lab {
    double l, a, b;
};
Lab oklab(std::uint32_t rgb) {
    auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    const double r = lin(((rgb >> 16) & 0xFF) / 255.0);
    const double g = lin(((rgb >> 8) & 0xFF) / 255.0);
    const double b = lin((rgb & 0xFF) / 255.0);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
double distance(std::uint32_t x, std::uint32_t y) {
    const Lab a = oklab(x);
    const Lab b = oklab(y);
    return std::sqrt((a.l - b.l) * (a.l - b.l) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b));
}
double hueDegrees(std::uint32_t rgb) {
    const Lab c = oklab(rgb);
    return std::fmod(std::atan2(c.b, c.a) * 180.0 / 3.14159265358979 + 360.0, 360.0);
}

double cr(std::uint32_t argbA, std::uint32_t argbB) {
    return contrastRatio(toRgba(argbA | 0xFF000000u), toRgba(argbB | 0xFF000000u));
}

// Every rule PaletteTests holds the five presets to, for the accent now set; what fails, in words.
void checkRules(std::uint32_t rgb, std::vector<std::string>& failures) {
    auto fail = [&](ThemeKind theme, const std::string& what, double value) {
        if (failures.size() < 40) {
            failures.push_back(std::format("#{:06X} theme {} {} ({:.2f})", rgb, static_cast<int>(theme), what, value));
        }
    };
    for (const ThemeKind theme : kThemes) {
        const Rgba hover = color(theme, C::BgHover);
        const Rgba panel = color(theme, C::BgPanel);
        const Rgba subtle = color(theme, C::AccentSubtle);
        const Rgba hoverOnPanel = over(hover, panel);
        // Body / secondary / tertiary text on a selected row.
        if (const double v = contrastRatio(color(theme, C::TextPrimary), subtle); v < 4.5) fail(theme, "text.primary on subtle", v);
        if (const double v = contrastRatio(color(theme, C::TextSecondary), subtle); v < 4.5) fail(theme, "text.secondary on subtle", v);
        if (const double v = contrastRatio(color(theme, C::TextTertiary), subtle); v < 3.0) fail(theme, "text.tertiary on subtle", v);
        // Accent text on every surface it lands on and on the selection.
        const Rgba text = color(theme, C::AccentText);
        for (const C s : {C::BgBase, C::BgTitleBar, C::BgChrome, C::BgPanel, C::BgRaised, C::BgOverlay, C::BgInput}) {
            if (const double v = contrastRatio(text, color(theme, s)); v < 4.5) fail(theme, std::string("accent.text on ") + tokens::kColorNames[static_cast<std::size_t>(s)], v);
        }
        if (const double v = contrastRatio(text, hoverOnPanel); v < 4.5) fail(theme, "accent.text on hover", v);
        if (const double v = contrastRatio(text, subtle); v < 4.5) fail(theme, "accent.text on subtle", v);
        // White text on the button at rest, hovered, pressed.
        for (const C fill : {C::AccentBase, C::AccentHover, C::AccentPressed}) {
            if (const double v = contrastRatio(color(theme, C::TextOnAccent), color(theme, fill)); v < 4.5) fail(theme, "white on fill", v);
        }
        // The fill as a mark; the focus ring.
        for (const C s : {C::BgBase, C::BgChrome, C::BgPanel}) {
            if (const double v = contrastRatio(color(theme, C::AccentBase), color(theme, s)); v < 3.0) fail(theme, "mark", v);
        }
        for (const C s : {C::BgBase, C::BgChrome, C::BgPanel, C::BgRaised, C::BgOverlay, C::BgInput}) {
            if (const double v = contrastRatio(color(theme, C::AccentFocus), color(theme, s)); v < 3.0) fail(theme, "focus", v);
        }
        // The selection apart from the card and from the hover fill.
        if (const int v = largestChannelStep(subtle, panel); v < 8) fail(theme, "subtle vs panel", v);
        if (const int v = largestChannelStep(subtle, hoverOnPanel); v < 6) fail(theme, "subtle vs hover", v);
    }
    // High contrast keeps its own.
    if (colorArgb(ThemeKind::HighContrast, C::AccentBase) != tokens::kHighContrast[static_cast<std::size_t>(C::AccentBase)]) {
        fail(ThemeKind::HighContrast, "hc accent changed", 0);
    }
}

std::vector<std::uint32_t> sweep() {
    std::vector<std::uint32_t> colours = {0xFFFF00, 0xFFFFFF, 0x000000, 0x808080, 0x7F7F7F, 0x202020, 0xE0E0E0, 0xF5E663,
                                          0xFFF3A0, 0x4B0082, 0x5B2C9F, 0x2E1065, 0x008080, 0x14B8A6, 0x0F766E, 0xFF0000,
                                          0x00FF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0x101114, 0x25282C, 0xECEEF1, 0xFFC0CB};
    for (int h = 0; h < 360; h += 15) {
        for (const float s : {0.15f, 0.5f, 1.0f}) {
            for (const float v : {0.15f, 0.45f, 0.75f, 1.0f}) {
                colours.push_back(fromHsv({static_cast<float>(h), s, v}));
            }
        }
    }
    std::uint32_t seed = 0x1234567u; // a fixed sequence: the same colours every run
    for (int i = 0; i < 600; ++i) {
        seed = seed * 1664525u + 1013904223u;
        colours.push_back(seed >> 8);
    }
    return colours;
}

} // namespace

TEST_CASE("D-143: the rules give D-137's tables for the presets") {
    struct Expect {
        Accent accent;
        bool handTuned; // D-137 softened its dark readable tone by hand
    };
    for (const Expect e : {Expect{Accent::Blue, false}, Expect{Accent::Copper, false}, Expect{Accent::Sea, true},
                           Expect{Accent::Pomegranate, false}, Expect{Accent::Olive, true}}) {
        const std::uint32_t rgb = presetColor(e.accent);
        CAPTURE(std::format("#{:06X}", rgb));
        const AccentDerivation rules = deriveAccent(rgb);
        const AccentDerivation table = accentTokens(rgb);
        CHECK_FALSE(rules.darkened);
        CHECK_FALSE(rules.lightened);
        CHECK(rules.light == table.light);
        for (const auto& [derived, kept] : {std::pair{rules.dark, table.dark}, std::pair{rules.black, table.black}}) {
            CHECK(derived.base == kept.base);
            CHECK(derived.hover == kept.hover);
            CHECK(derived.pressed == kept.pressed);
            CHECK(derived.subtle == kept.subtle);
            CHECK(derived.onAccent == kept.onAccent);
            if (e.handTuned) {
                // Sea: #52AFA8 for #4FB1AA, Olive: #8DAA5E for #93AD5F — under the OKLab step one can just see
                // (~0.02); the preset keeps its table, so nothing changes for whoever chose it.
                CHECK(derived.text != kept.text);
                CHECK(distance(derived.text, kept.text) < 0.015);
            } else {
                CHECK(derived.text == kept.text);
            }
            CHECK(derived.focus == derived.text);
        }
    }
    // Mavi's rules are the tokens' own values.
    const AccentDerivation blue = deriveAccent(kDefaultAccent);
    auto token = [](const auto& theme, C c) { return theme[static_cast<std::size_t>(c)]; };
    for (const auto& [set, theme] : {std::pair{blue.dark, tokens::kDark}, std::pair{blue.light, tokens::kLight}, std::pair{blue.black, tokens::kBlack}}) {
        CHECK(set.base == token(theme, C::AccentBase));
        CHECK(set.hover == token(theme, C::AccentHover));
        CHECK(set.pressed == token(theme, C::AccentPressed));
        CHECK(set.subtle == token(theme, C::AccentSubtle));
        CHECK(set.text == token(theme, C::AccentText));
        CHECK(set.focus == token(theme, C::AccentFocus));
        CHECK(set.onAccent == token(theme, C::TextOnAccent));
    }
}

TEST_CASE("D-143: any colour keeps every contrast rule, in dark, light and pure black") {
    std::vector<std::string> failures;
    const auto colours = sweep();
    for (const std::uint32_t rgb : colours) {
        setAccentColor(rgb);
        checkRules(rgb, failures);
    }
    setAccentColor(kDefaultAccent);
    const std::string first = failures.empty() ? std::string() : failures.front();
    INFO(first);
    CHECK(failures.empty());
    CHECK(colours.size() > 900);
}

TEST_CASE("D-143: a light colour is darkened for white text, a dark one lifted for the dark themes, and said so") {
    // Pure yellow: white text cannot read on it.
    const AccentDerivation yellow = deriveAccent(0xFFFF00);
    CHECK(yellow.darkened);
    CHECK_FALSE(yellow.lightened);
    CHECK((yellow.light.base & 0xFFFFFFu) != 0xFFFF00u);
    CHECK(cr(yellow.light.base, 0xFFFFFF) >= 4.6);
    CHECK(std::abs(hueDegrees(yellow.light.base) - hueDegrees(0xFFFF00)) < 4.0); // still a yellow (an olive one)
    CHECK(yellow.dark.base == yellow.light.base); // one fill in every theme when only white text asked
    // White and grey: a mid grey.
    for (const std::uint32_t grey : {0xFFFFFFu, 0xE0E0E0u, 0x808080u}) {
        const AccentDerivation d = deriveAccent(grey);
        const std::uint32_t base = d.light.base & 0xFFFFFFu;
        CHECK(((base >> 16) & 0xFF) == (base & 0xFF)); // stays grey
        CHECK(cr(d.light.base, 0xFFFFFF) >= 4.6);
    }
    // Deep purple: kept in the light theme, lifted in the dark ones (a mark on the dark card).
    const AccentDerivation purple = deriveAccent(0x4B0082);
    CHECK_FALSE(purple.darkened);
    CHECK(purple.lightened);
    CHECK((purple.light.base & 0xFFFFFFu) == 0x4B0082u);
    CHECK(purple.dark.base != purple.light.base);
    CHECK(purple.black.base == purple.dark.base);
    CHECK(cr(purple.dark.base, tokens::kDark[static_cast<std::size_t>(C::BgPanel)]) >= 3.0);
    CHECK(cr(purple.dark.base, 0xFFFFFF) >= 4.6);
    CHECK(std::abs(hueDegrees(purple.dark.base) - hueDegrees(0x4B0082)) < 6.0);
    // Black: black fill on light, a grey one on dark.
    const AccentDerivation black = deriveAccent(0x000000);
    CHECK((black.light.base & 0xFFFFFFu) == 0u);
    CHECK(black.lightened);
    // A preset needs neither.
    for (int i = 0; i < kAccentCount; ++i) {
        const AccentDerivation d = deriveAccent(presetColor(static_cast<Accent>(i)));
        CHECK_FALSE(d.darkened);
        CHECK_FALSE(d.lightened);
    }
}

TEST_CASE("D-143: the accent set is what the tokens give; Mavi is the tokens' own; high contrast keeps its own") {
    const std::uint32_t teal = 0x0F766E;
    setAccentColor(teal);
    CHECK(accentColor() == teal);
    const AccentDerivation d = deriveAccent(teal);
    CHECK(colorArgb(ThemeKind::Dark, C::AccentBase) == d.dark.base);
    CHECK(colorArgb(ThemeKind::Light, C::AccentText) == d.light.text);
    CHECK(colorArgb(ThemeKind::Black, C::AccentSubtle) == d.black.subtle);
    CHECK(colorArgb(ThemeKind::Dark, C::BgPanel) == tokens::kDark[static_cast<std::size_t>(C::BgPanel)]); // surfaces stay
    CHECK(colorArgb(ThemeKind::HighContrast, C::AccentBase) == tokens::kHighContrast[static_cast<std::size_t>(C::AccentBase)]);
    setAccent(Accent::Sea);
    CHECK(colorArgb(ThemeKind::Dark, C::AccentText) == 0xFF4FB1AAu); // the preset's table, not the rules
    setAccentColor(kDefaultAccent);
    for (const ThemeKind theme : {ThemeKind::Dark, ThemeKind::Light, ThemeKind::Black}) {
        CHECK(colorArgb(theme, C::AccentBase) == (theme == ThemeKind::Light ? tokens::kLight : theme == ThemeKind::Black ? tokens::kBlack : tokens::kDark)[static_cast<std::size_t>(C::AccentBase)]);
    }
    // The top byte of a colour is ignored.
    setAccentColor(0xFF000000u | teal);
    CHECK(accentColor() == teal);
    setAccentColor(kDefaultAccent);
}

TEST_CASE("D-143: presets by colour") {
    for (int i = 0; i < kAccentCount; ++i) {
        const auto accent = static_cast<Accent>(i);
        REQUIRE(presetOf(presetColor(accent)).has_value());
        CHECK(*presetOf(presetColor(accent)) == accent);
    }
    CHECK(presetColor(Accent::Blue) == kDefaultAccent);
    CHECK_FALSE(presetOf(0x7A3EC8).has_value());
}

TEST_CASE("D-143: hex colours") {
    CHECK(parseHexColor(L"#3070D9") == 0x3070D9u);
    CHECK(parseHexColor(L"3070d9") == 0x3070D9u);
    CHECK(parseHexColor(L"  #3070D9 ") == 0x3070D9u);
    CHECK(parseHexColor(L"#abc") == 0xAABBCCu);
    CHECK(parseHexColor(L"#000000") == 0u);
    for (const wchar_t* bad : {L"", L"#", L"#12345", L"#1234567", L"#GGGGGG", L"rgb(1,2,3)", L"# 3070D9", L"#30 70D9"}) {
        CAPTURE(std::wstring(bad));
        CHECK_FALSE(parseHexColor(bad).has_value());
    }
    CHECK(hexColor(0x3070D9) == L"#3070D9");
    CHECK(hexColor(0x00000A) == L"#00000A");
    CHECK(hexColor(0xFF3070D9u) == L"#3070D9");
    for (const std::uint32_t rgb : sweep()) {
        CHECK(parseHexColor(hexColor(rgb)) == (rgb & 0xFFFFFFu));
    }
}

TEST_CASE("D-143: the picker's HSV goes there and back") {
    int off = 0;
    for (const std::uint32_t rgb : sweep()) {
        const std::uint32_t back = fromHsv(toHsv(rgb));
        const int dr = std::abs(static_cast<int>((back >> 16) & 0xFF) - static_cast<int>((rgb >> 16) & 0xFF));
        const int dg = std::abs(static_cast<int>((back >> 8) & 0xFF) - static_cast<int>((rgb >> 8) & 0xFF));
        const int db = std::abs(static_cast<int>(back & 0xFF) - static_cast<int>(rgb & 0xFF));
        off = std::max({off, dr, dg, db});
    }
    CHECK(off <= 1); // float rounding at most one level
    const Hsv red = toHsv(0xFF0000);
    CHECK(red.h == doctest::Approx(0.0f));
    CHECK(red.s == doctest::Approx(1.0f));
    CHECK(red.v == doctest::Approx(1.0f));
    CHECK(toHsv(0x00FF00).h == doctest::Approx(120.0f));
    CHECK(toHsv(0x0000FF).h == doctest::Approx(240.0f));
    CHECK(fromHsv({360.0f, 1, 1}) == 0xFF0000u);
    CHECK(fromHsv({-120.0f, 1, 1}) == 0x0000FFu);
    CHECK(fromHsv({200.0f, 0, 0.5f}) == 0x808080u);
}

TEST_CASE("D-143: the picker — a drag previews, letting go commits, the box takes a colour, Esc goes back") {
    Host host(HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, wl::test::graphics().text.get()});
    host.setRoot(std::make_unique<Widget>());
    host.layout({700, 500});
    std::vector<std::uint32_t> previews;
    std::vector<std::uint32_t> commits;
    int cancels = 0;
    int closes = 0;
    auto popup = std::make_unique<ColorPickerPopup>(RectF{20, 20, 180, 28}, ColorPicker::Texts{L"field", L"hue", L"#RRGGBB", L"Aa", L"dark", L"light"},
                                                    0x3070D9);
    ColorPickerPopup* raw = popup.get();
    raw->onPreview = [&](std::uint32_t c) { previews.push_back(c); };
    raw->onCommit = [&](std::uint32_t c) { commits.push_back(c); };
    raw->onCancel = [&] { ++cancels; };
    raw->onClosed = [&] { ++closes; };
    host.pushModal(std::move(popup), raw, /*scrim=*/false);
    host.layout({700, 500});
    const RectF panel = raw->panel();
    CHECK(panel.y >= 48.0f); // under the control it opened from
    const RectF picker = raw->picker().bounds();
    auto pointer = [&](PointerAction action, float x, float y) { host.onPointer({action, {x, y}, HitZone::Client}); };

    // Drag in the field: the top right corner is the hue at full strength.
    const float fx = picker.x + ColorPicker::kWidth - 1;
    const float fy = picker.y + 1;
    pointer(PointerAction::Move, picker.x + 50, picker.y + 50);
    pointer(PointerAction::Down, picker.x + 50, picker.y + 50);
    pointer(PointerAction::Move, fx, fy);
    CHECK(previews.size() >= 2);
    CHECK(commits.empty());
    pointer(PointerAction::Up, fx, fy);
    REQUIRE(commits.size() == 1);
    const Hsv picked = toHsv(commits.back());
    CHECK(picked.s > 0.98f);
    CHECK(picked.v > 0.98f);
    CHECK(std::abs(picked.h - toHsv(0x3070D9).h) < 1.0f); // the hue it opened with
    CHECK(raw->picker().color() == commits.back());

    // The hue bar's middle: cyan-ish (180°).
    const float hueY = picker.y + ColorPicker::kFieldHeight + tokens::spacing::s5 + ColorPicker::kHueHeight / 2;
    pointer(PointerAction::Move, picker.x + ColorPicker::kWidth / 2, hueY);
    pointer(PointerAction::Down, picker.x + ColorPicker::kWidth / 2, hueY);
    pointer(PointerAction::Up, picker.x + ColorPicker::kWidth / 2, hueY);
    REQUIRE(commits.size() == 2);
    CHECK(std::abs(toHsv(commits.back()).h - 180.0f) < 2.0f);

    // Typed: a colour is previewed once it is one, committed with Enter; half a colour is not.
    const float boxY = hueY + ColorPicker::kHueHeight / 2 + tokens::spacing::s5 + tokens::size::control / 2;
    const float boxX = picker.x + tokens::size::control + tokens::spacing::s4 + 20;
    pointer(PointerAction::Move, boxX, boxY);
    pointer(PointerAction::Down, boxX, boxY);
    pointer(PointerAction::Up, boxX, boxY);
    host.onKeyDown({'A', true, false, false}); // Ctrl+A: what is there goes
    const std::size_t before = previews.size();
    for (const wchar_t ch : std::wstring(L"#0F766")) {
        host.onChar(ch);
    }
    CHECK(previews.size() == before); // "#0F766" is not a colour yet
    host.onChar(L'E');
    REQUIRE(previews.size() == before + 1);
    CHECK(previews.back() == 0x0F766Eu);
    host.onKeyDown({VK_RETURN, false, false, false});
    REQUIRE(commits.size() == 3);
    CHECK(commits.back() == 0x0F766Eu);
    CHECK(raw->picker().hsv().h == doctest::Approx(toHsv(0x0F766E).h));

    // Esc in the box: not "clear the text" but the popup's — back to the opening colour, closed.
    host.onKeyDown({VK_ESCAPE, false, false, false});
    CHECK(cancels == 1);
    CHECK(closes == 1);
    CHECK_FALSE(host.hasModal());
}

TEST_CASE("D-143: the picker closes on a click outside, keeping the colour") {
    Host host(HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, wl::test::graphics().text.get()});
    host.setRoot(std::make_unique<Widget>());
    host.layout({700, 500});
    int cancels = 0;
    int closes = 0;
    auto popup = std::make_unique<ColorPickerPopup>(RectF{20, 440, 180, 28}, ColorPicker::Texts{}, 0x7A3EC8);
    ColorPickerPopup* raw = popup.get();
    raw->onCancel = [&] { ++cancels; };
    raw->onClosed = [&] { ++closes; };
    host.pushModal(std::move(popup), raw, /*scrim=*/false);
    host.layout({700, 500});
    CHECK(raw->panel().bottom() <= 440.0f); // no room below: above the control
    CHECK(raw->picker().color() == 0x7A3EC8u);
    // The arrows on the opened popup move the field's marker (and commit).
    std::uint32_t committed = 0;
    raw->onCommit = [&](std::uint32_t c) { committed = c; };
    host.onKeyDown({VK_RIGHT, false, false, false});
    CHECK(committed != 0u);
    host.onPointer({PointerAction::Move, {690, 10}, HitZone::Client});
    host.onPointer({PointerAction::Down, {690, 10}, HitZone::Client});
    CHECK(cancels == 0);
    CHECK(closes == 1);
    CHECK_FALSE(host.hasModal());
}
