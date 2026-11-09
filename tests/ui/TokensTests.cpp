#include "ui/theme/Palette.h"

#include <doctest.h>

using namespace wl::ui;

TEST_CASE("token tables match the reference tool's look (D-109)") {
    CHECK(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgBase)] == 0xFF202020u);
    CHECK(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgPanel)] == 0xFF2B2B2Bu);
    CHECK(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgChrome)] == 0xFF262626u);
    CHECK(tokens::kLight[static_cast<std::size_t>(tokens::Color::BgBase)] == 0xFFEEEEEEu);
    CHECK(tokens::kLight[static_cast<std::size_t>(tokens::Color::BgPanel)] == 0xFFFBFBFBu);
    CHECK(tokens::kLight[static_cast<std::size_t>(tokens::Color::BgChrome)] == 0xFFF5F5F5u);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::AccentBase) == 0xFF4CC2FFu); // Windows' accent on dark
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::AccentBase) == 0xFF0067C0u);
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::AccentText) == 0xFF0058CDu);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::TierNotRecommended) == 0xFFF54137u);
    CHECK(colorArgb(ThemeKind::HighContrast, tokens::Color::AccentBase) == 0xFF1AEBFFu);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::Scrim) == 0x99000000u);
}

TEST_CASE("toRgba unpacks ARGB") {
    const Rgba c = toRgba(0x80FF0000u);
    CHECK(c.r == doctest::Approx(1.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("The reference tool's density (D-109)") {
    // One 12px Segoe UI, 23.5 rows, 24 controls - measured on the reference tool at 175 %.
    CHECK(tokens::size::titleBar == 34.0f);
    CHECK(tokens::size::statusBar == 22.0f);
    CHECK(tokens::size::ribbon == 50.0f);
    CHECK(tokens::size::control == 24.0f);
    CHECK(tokens::size::tableRow == 23.5f);
    CHECK(tokens::size::navItem == 28.0f);
    CHECK(tokens::size::checkbox == 13.0f);
    CHECK(tokens::radius::button == 4.0f);
    CHECK(tokens::radius::r3 == 6.0f); // cards
    CHECK(tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::Body)].size == 12.0f);
    for (const auto& style : tokens::kTypeStyles) {
        CHECK(style.size <= 15.0f);            // nothing large
        CHECK(style.lineHeight >= style.size); // no style clips its own glyphs
    }
}

TEST_CASE("text contrast meets WCAG AA in dark and light themes") {
    for (const auto theme : {ThemeKind::Dark, ThemeKind::Light}) {
        CAPTURE(static_cast<int>(theme));
        const Rgba base = color(theme, tokens::Color::BgBase);
        const Rgba panel = color(theme, tokens::Color::BgPanel);
        for (const auto ink : {tokens::Color::TextPrimary, tokens::Color::TextSecondary}) {
            CHECK(contrastRatio(color(theme, ink), base) >= 4.5);
            CHECK(contrastRatio(color(theme, ink), panel) >= 4.5);
        }
        // D-109: the reference tool's grey captions and column heads read at about 3:1 - kept at >= 3.
        CHECK(contrastRatio(color(theme, tokens::Color::TextTertiary), base) >= 3.0);
        CHECK(contrastRatio(color(theme, tokens::Color::TextTertiary), panel) >= 3.0);
        CHECK(contrastRatio(color(theme, tokens::Color::TextOnAccent), color(theme, tokens::Color::AccentBase)) >= 4.5);
    }
}

TEST_CASE("bestContrast picks the readable glyph color") {
    // The close button's hover (caption.close) carries a readable glyph in every theme.
    for (const auto theme : {ThemeKind::Dark, ThemeKind::Light, ThemeKind::HighContrast, ThemeKind::Black}) {
        const auto glyph = bestContrast(theme, tokens::Color::CaptionClose, tokens::Color::TextPrimary,
                                        tokens::Color::TextOnAccent);
        CHECK(contrastRatio(color(theme, glyph), color(theme, tokens::Color::CaptionClose)) >= 3.0);
    }
}

TEST_CASE("every accent color meets the contrast rules of the tokens, in dark and light") {
    for (int i = 0; i < kAccentCount; ++i) {
        setAccent(static_cast<Accent>(i));
        CAPTURE(i);
        for (const ThemeKind theme : {ThemeKind::Dark, ThemeKind::Light}) {
            CAPTURE(static_cast<int>(theme));
            const Rgba accentBase = color(theme, tokens::Color::AccentBase);
            const Rgba accentText = color(theme, tokens::Color::AccentText);
            // White text on an accent button; the accent text (links, counts, percentages) on the
            // page, on panels and on the subtle accent fill (a selected row).
            CHECK(contrastRatio(color(theme, tokens::Color::TextOnAccent), accentBase) >= 4.5);
            CHECK(contrastRatio(accentText, color(theme, tokens::Color::BgBase)) >= 4.5);
            CHECK(contrastRatio(accentText, color(theme, tokens::Color::BgPanel)) >= 4.5);
            CHECK(contrastRatio(accentText, color(theme, tokens::Color::AccentSubtle)) >= 4.0);
            // The fill as a mark (checkbox, switch, tab underline) against the page.
            CHECK(contrastRatio(accentBase, color(theme, tokens::Color::BgBase)) >= 3.0);
        }
        // High contrast keeps its own accent whatever is picked.
        CHECK(colorArgb(ThemeKind::HighContrast, tokens::Color::AccentBase) == tokens::kHighContrast[static_cast<std::size_t>(tokens::Color::AccentBase)]);
    }
    setAccent(Accent::Blue);
    // Blue is the package's token set, untouched.
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::AccentBase) == tokens::kDark[static_cast<std::size_t>(tokens::Color::AccentBase)]);
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::AccentSubtle) == tokens::kLight[static_cast<std::size_t>(tokens::Color::AccentSubtle)]);
    setAccent(Accent::Sea);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::AccentBase) == 0xFF147971u);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::BgBase) == tokens::kDark[static_cast<std::size_t>(tokens::Color::BgBase)]); // surfaces stay
    setAccent(Accent::Blue);
}

TEST_CASE("D-107: surfaces step clearly in both themes") {
    auto lum = [](ThemeKind theme, tokens::Color c) {
        const Rgba v = color(theme, c);
        return 0.2126 * v.r + 0.7152 * v.g + 0.0722 * v.b;
    };
    // Dark: page < panel < raised < pressed; light: the panel is white over a grey page.
    CHECK(lum(ThemeKind::Dark, tokens::Color::BgBase) < lum(ThemeKind::Dark, tokens::Color::BgPanel));
    CHECK(lum(ThemeKind::Dark, tokens::Color::BgPanel) < lum(ThemeKind::Dark, tokens::Color::BgRaised));
    CHECK(lum(ThemeKind::Dark, tokens::Color::BgRaised) < lum(ThemeKind::Dark, tokens::Color::BgPressed));
    CHECK(lum(ThemeKind::Light, tokens::Color::BgPanel) > lum(ThemeKind::Light, tokens::Color::BgBase));
    for (const auto theme : {ThemeKind::Dark, ThemeKind::Light}) {
        CHECK(contrastRatio(color(theme, tokens::Color::LineSubtle), color(theme, tokens::Color::LineStrong)) > 1.05);
    }
}
