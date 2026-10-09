#include "ui/theme/Palette.h"

#include <doctest.h>

using namespace wl::ui;

TEST_CASE("token tables match the handoff") {
    CHECK(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgBase)] == 0xFF1A1918u); // the table; D-091 draws darker
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::AccentBase) == 0xFFA85C24u);
    CHECK(colorArgb(ThemeKind::HighContrast, tokens::Color::AccentBase) == 0xFF1AEBFFu);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::Scrim) == 0x99000000u);
}

TEST_CASE("toRgba unpacks ARGB") {
    const Rgba c = toRgba(0x80FF0000u);
    CHECK(c.r == doctest::Approx(1.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("design size limits hold (minimal, compact UI)") {
    CHECK(tokens::size::control == 24.0f);
    CHECK(tokens::size::controlLarge <= 28.0f);
    CHECK(tokens::radius::r3 <= 4.0f);
    for (const auto& style : tokens::kTypeStyles) {
        CHECK(style.size <= 16.0f);
    }
}

TEST_CASE("text contrast meets WCAG AA in dark and light themes") {
    for (const auto theme : {ThemeKind::Dark, ThemeKind::Light}) {
        CAPTURE(static_cast<int>(theme));
        const Rgba base = color(theme, tokens::Color::BgBase);
        CHECK(contrastRatio(color(theme, tokens::Color::TextPrimary), base) >= 4.5);
        CHECK(contrastRatio(color(theme, tokens::Color::TextSecondary), base) >= 4.5);
        CHECK(contrastRatio(color(theme, tokens::Color::TextOnAccent), color(theme, tokens::Color::AccentBase)) >= 4.5);
    }
}

TEST_CASE("bestContrast picks the readable glyph color") {
    // Close button hover: glyph on status.error must stay readable in every theme.
    for (const auto theme : {ThemeKind::Dark, ThemeKind::Light, ThemeKind::HighContrast}) {
        const auto glyph = bestContrast(theme, tokens::Color::StatusError, tokens::Color::TextPrimary,
                                        tokens::Color::TextOnAccent);
        CHECK(contrastRatio(color(theme, glyph), color(theme, tokens::Color::StatusError)) >= 3.0);
    }
}

TEST_CASE("every accent color meets the contrast rules of the tokens, in dark and light") {
    for (int i = 0; i < kAccentCount; ++i) {
        setAccent(static_cast<Accent>(i));
        CAPTURE(i);
        for (const ThemeKind theme : {ThemeKind::Dark, ThemeKind::Light}) {
            CAPTURE(static_cast<int>(theme));
            const Rgba accentBase = color(theme, tokens::Color::AccentBase);
            // Text on an accent button, the accent as text / icon on the page and on panels.
            CHECK(contrastRatio(color(theme, tokens::Color::TextOnAccent), accentBase) >= 4.5);
            CHECK(contrastRatio(accentBase, color(theme, tokens::Color::BgBase)) >= 4.4);
            CHECK(contrastRatio(accentBase, color(theme, tokens::Color::BgPanel)) >= 3.0);
            CHECK(contrastRatio(accentBase, color(theme, tokens::Color::AccentSubtle)) >= 3.0);
        }
        // High contrast keeps its own accent whatever is picked.
        CHECK(colorArgb(ThemeKind::HighContrast, tokens::Color::AccentBase) == tokens::kHighContrast[static_cast<std::size_t>(tokens::Color::AccentBase)]);
    }
    setAccent(Accent::Copper);
    // Copper is the handoff's token set, untouched.
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::AccentBase) == tokens::kDark[static_cast<std::size_t>(tokens::Color::AccentBase)]);
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::AccentSubtle) == tokens::kLight[static_cast<std::size_t>(tokens::Color::AccentSubtle)]);
    setAccent(Accent::Sky);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::AccentBase) == 0xFF7FA7D9u); // the screen 19 swatch
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::BgBase) == 0xFF111010u); // the accent leaves surfaces alone
    setAccent(Accent::Copper);
}

TEST_CASE("D-091: the dark surfaces are near black, each layer a clear step lighter") {
    auto lum = [](tokens::Color c) {
        const Rgba v = color(ThemeKind::Dark, c);
        return 0.2126 * v.r + 0.7152 * v.g + 0.0722 * v.b;
    };
    CHECK(lum(tokens::Color::BgBase) < toRgba(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgBase)]).g);
    CHECK(lum(tokens::Color::BgInput) < lum(tokens::Color::BgBase));
    CHECK(lum(tokens::Color::BgBase) < lum(tokens::Color::BgPanel));
    CHECK(lum(tokens::Color::BgPanel) < lum(tokens::Color::BgRaised));
    CHECK(lum(tokens::Color::BgRaised) < lum(tokens::Color::BgPressed));
    CHECK(lum(tokens::Color::LineSubtle) < lum(tokens::Color::LineStrong));
    // Light and high contrast are the handoff's.
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::BgBase) == tokens::kLight[static_cast<std::size_t>(tokens::Color::BgBase)]);
}
