#include "ui/theme/Palette.h"

#include <doctest.h>

#include <string>

using namespace wl::ui;

TEST_CASE("token tables carry WinLove's own colours (D-137; were the reference tool's, D-109)") {
    // Slate-biased neutrals: a deep page under the frame and the cards; the accent WinLove's own blue,
    // one fill in both themes with white text (was the Windows accent, #4CC2FF / #0067C0).
    CHECK(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgBase)] == 0xFF17191Cu);
    CHECK(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgPanel)] == 0xFF25282Cu);
    CHECK(tokens::kDark[static_cast<std::size_t>(tokens::Color::BgChrome)] == 0xFF1F2125u);
    CHECK(tokens::kLight[static_cast<std::size_t>(tokens::Color::BgBase)] == 0xFFECEEF1u);
    CHECK(tokens::kLight[static_cast<std::size_t>(tokens::Color::BgPanel)] == 0xFFFFFFFFu);
    CHECK(tokens::kLight[static_cast<std::size_t>(tokens::Color::BgChrome)] == 0xFFF7F8FAu);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::AccentBase) == 0xFF3070D9u);
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::AccentBase) == 0xFF3070D9u);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::TextOnAccent) == 0xFFFFFFFFu);
    CHECK(colorArgb(ThemeKind::Light, tokens::Color::AccentText) == 0xFF2462C7u);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::TierNotRecommended) == colorArgb(ThemeKind::Dark, tokens::Color::StatusError));
    CHECK(colorArgb(ThemeKind::HighContrast, tokens::Color::AccentBase) == 0xFF1AEBFFu);
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::Scrim) == 0x99000000u);
    CHECK(std::string(tokens::kVersion) == "5.0.0");
}

TEST_CASE("toRgba unpacks ARGB") {
    const Rgba c = toRgba(0x80FF0000u);
    CHECK(c.r == doctest::Approx(1.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("WinLove's density (D-136; was the reference tool's of D-109)") {
    // 13px Segoe UI body text, 28 rows and controls, 32 navigation items, rounder corners - the look
    // the owner approved on before/after renders (2026-11-10). The ribbon band stays as it was.
    CHECK(tokens::size::titleBar == 36.0f);
    CHECK(tokens::size::statusBar == 26.0f);
    CHECK(tokens::size::ribbon == 50.0f);
    CHECK(tokens::size::control == 28.0f);
    CHECK(tokens::size::tableRow == 28.0f);
    CHECK(tokens::size::navItem == 32.0f);
    CHECK(tokens::size::checkbox == 14.0f);
    CHECK(tokens::radius::button == 6.0f);
    CHECK(tokens::radius::r3 == 10.0f); // cards
    CHECK(tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::Body)].size == 13.0f);
    for (const auto& style : tokens::kTypeStyles) {
        CHECK(style.size <= 17.0f);            // nothing large
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
    CHECK(colorArgb(ThemeKind::Dark, tokens::Color::AccentBase) == 0xFF177F77u); // D-137: a shade lighter (a mark on the dark panel)
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
