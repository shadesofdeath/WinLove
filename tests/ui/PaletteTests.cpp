// D-137: WinLove's own palette — every text-on-surface pair the UI draws, in every theme and with every
// accent, read at WCAG AA (body text >= 4.5:1, secondary marks and large / tertiary text >= 3:1); the
// layers, the selection and the hover fills, the lines and the disabled text stay distinct.
#include "ui/theme/Palette.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

using namespace wl::ui;
using C = tokens::Color;

namespace {

// `top` (may be translucent, e.g. bg.hover) laid over the opaque `bottom`.
Rgba over(Rgba top, Rgba bottom) {
    const float a = top.a;
    return {top.r * a + bottom.r * (1 - a), top.g * a + bottom.g * (1 - a), top.b * a + bottom.b * (1 - a), 1.0f};
}

struct Surface {
    const char* name;
    Rgba color;
};

// Everything text is drawn on: the page, the frame (title bar, ribbon band, status bar, navigation card),
// the content card, its groups, menus / dialogs / tooltips, inputs, push buttons, a selected row, a hovered
// row (on the card and on a group), a pressed item and the four status fills.
std::vector<Surface> textSurfaces(ThemeKind theme) {
    const Rgba hover = color(theme, C::BgHover);
    return {
        {"bg.base", color(theme, C::BgBase)},
        {"bg.titleBar", color(theme, C::BgTitleBar)},
        {"bg.chrome", color(theme, C::BgChrome)},
        {"bg.panel", color(theme, C::BgPanel)},
        {"bg.raised", color(theme, C::BgRaised)},
        {"bg.overlay", color(theme, C::BgOverlay)},
        {"bg.input", color(theme, C::BgInput)},
        {"bg.control", color(theme, C::BgControl)},
        {"bg.pressed", color(theme, C::BgPressed)},
        {"accent.subtle (selected)", color(theme, C::AccentSubtle)},
        {"bg.hover over bg.panel", over(hover, color(theme, C::BgPanel))},
        {"bg.hover over bg.raised", over(hover, color(theme, C::BgRaised))},
        {"bg.hover over bg.overlay", over(hover, color(theme, C::BgOverlay))},
        {"status.successSubtle", color(theme, C::StatusSuccessSubtle)},
        {"status.warningSubtle", color(theme, C::StatusWarningSubtle)},
        {"status.errorSubtle", color(theme, C::StatusErrorSubtle)},
        {"status.infoSubtle", color(theme, C::StatusInfoSubtle)},
    };
}

// The surfaces coloured text (accent text, status words, "değiştirilecek", links) is drawn on.
std::vector<Surface> inkSurfaces(ThemeKind theme) {
    return {
        {"bg.base", color(theme, C::BgBase)},
        {"bg.titleBar", color(theme, C::BgTitleBar)},
        {"bg.chrome", color(theme, C::BgChrome)},
        {"bg.panel", color(theme, C::BgPanel)},
        {"bg.raised", color(theme, C::BgRaised)},
        {"bg.overlay", color(theme, C::BgOverlay)},
        {"bg.input", color(theme, C::BgInput)},
        {"bg.hover over bg.panel", over(color(theme, C::BgHover), color(theme, C::BgPanel))},
    };
}

void checkAtLeast(ThemeKind theme, C ink, const std::vector<Surface>& surfaces, double minimum) {
    for (const Surface& s : surfaces) {
        CAPTURE(s.name);
        CAPTURE(tokens::kColorNames[static_cast<std::size_t>(ink)]);
        CHECK(contrastRatio(color(theme, ink), s.color) >= minimum);
    }
}

constexpr ThemeKind kThemes[] = {ThemeKind::Dark, ThemeKind::Light, ThemeKind::Black};

int largestChannelStep(Rgba a, Rgba b) {
    return static_cast<int>(std::lround(255.0f * std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)})));
}

} // namespace

TEST_CASE("D-137: body and secondary text read at >= 4.5:1, tertiary at >= 3:1, on every surface, every theme") {
    for (const ThemeKind theme : kThemes) {
        for (int a = 0; a < kAccentCount; ++a) { // the selection fill follows the accent
            setAccent(static_cast<Accent>(a));
            CAPTURE(static_cast<int>(theme));
            CAPTURE(a);
            const auto surfaces = textSurfaces(theme);
            checkAtLeast(theme, C::TextPrimary, surfaces, 4.5);
            checkAtLeast(theme, C::TextSecondary, surfaces, 4.5);
            checkAtLeast(theme, C::TextTertiary, surfaces, 3.0);
        }
    }
    setAccent(Accent::Blue);
}

TEST_CASE("D-137: accent text, status words and the accent on its fill read at AA, every accent") {
    for (const ThemeKind theme : kThemes) {
        CAPTURE(static_cast<int>(theme));
        const auto inks = inkSurfaces(theme);
        // Status colours as words ("Dosya bulunamadı", "Riskli", "Atlandı") on every surface they land on.
        for (const C status : {C::StatusSuccess, C::StatusWarning, C::StatusError, C::StatusInfo}) {
            checkAtLeast(theme, status, inks, 4.5);
        }
        // A status icon on its own InfoBar fill: a graphic, >= 3:1.
        const std::pair<C, C> fills[] = {{C::StatusSuccess, C::StatusSuccessSubtle}, {C::StatusWarning, C::StatusWarningSubtle},
                                         {C::StatusError, C::StatusErrorSubtle}, {C::StatusInfo, C::StatusInfoSubtle}};
        for (const auto& [ink, fill] : fills) {
            CAPTURE(tokens::kColorNames[static_cast<std::size_t>(fill)]);
            CHECK(contrastRatio(color(theme, ink), color(theme, fill)) >= 3.0);
        }
        for (int a = 0; a < kAccentCount; ++a) {
            setAccent(static_cast<Accent>(a));
            CAPTURE(a);
            // Links, counts, "değiştirilecek": on every surface and on the selection fill (a chosen row).
            auto withSelection = inks;
            withSelection.push_back({"accent.subtle (selected)", color(theme, C::AccentSubtle)});
            checkAtLeast(theme, C::AccentText, withSelection, 4.5);
            // White text on the primary button at rest, hovered and pressed; a nav badge on the chosen item.
            for (const C fill : {C::AccentBase, C::AccentHover, C::AccentPressed}) {
                CHECK(contrastRatio(color(theme, C::TextOnAccent), color(theme, fill)) >= 4.5);
            }
            // The accent as a mark (tab underline, selection bar, progress, switch, checked ring) on the page,
            // the navigation card and the content card; the focus ring on everything that can hold focus.
            for (const C surface : {C::BgBase, C::BgChrome, C::BgPanel}) {
                CAPTURE(tokens::kColorNames[static_cast<std::size_t>(surface)]);
                CHECK(contrastRatio(color(theme, C::AccentBase), color(theme, surface)) >= 3.0);
            }
            for (const C surface : {C::BgBase, C::BgChrome, C::BgPanel, C::BgRaised, C::BgOverlay, C::BgInput}) {
                CAPTURE(tokens::kColorNames[static_cast<std::size_t>(surface)]);
                CHECK(contrastRatio(color(theme, C::AccentFocus), color(theme, surface)) >= 3.0);
            }
        }
    }
    setAccent(Accent::Blue);
}

TEST_CASE("D-137: template colours and toned icons stand out on the content card") {
    for (const ThemeKind theme : kThemes) {
        CAPTURE(static_cast<int>(theme));
        const Rgba panel = color(theme, C::BgPanel);
        const Rgba chrome = color(theme, C::BgChrome);
        // The Components page's template crescent on a tree check (a graphic).
        for (const C tier : {C::TierPrivacy, C::TierGeneral, C::TierLight, C::TierNotRecommended}) {
            CAPTURE(tokens::kColorNames[static_cast<std::size_t>(tier)]);
            CHECK(contrastRatio(color(theme, tier), panel) >= 3.0);
        }
        // Multicolour icons: the navigation (chrome), the ribbon (chrome) and the tables (panel).
        for (const C icon : {C::IconBlue, C::IconAmber, C::IconGreen, C::IconRed, C::IconPurple, C::IconTeal, C::IconSlate}) {
            CAPTURE(tokens::kColorNames[static_cast<std::size_t>(icon)]);
            CHECK(contrastRatio(color(theme, icon), panel) >= 3.0);
            CHECK(contrastRatio(color(theme, icon), chrome) >= 3.0);
        }
        // The size bar keeps text.primary readable at its deepest (30 % of the accent text over it).
        const Rgba bar = color(theme, C::SizeBar);
        const Rgba ink = color(theme, C::AccentText);
        const Rgba deepest{bar.r * 0.7f + ink.r * 0.3f, bar.g * 0.7f + ink.g * 0.3f, bar.b * 0.7f + ink.b * 0.3f, 1.0f};
        CHECK(contrastRatio(color(theme, C::TextPrimary), bar) >= 4.5);
        CHECK(contrastRatio(color(theme, C::TextPrimary), deepest) >= 4.5);
    }
}

TEST_CASE("D-137: the layers, the selection, the hover and the lines are distinct; disabled text stays legible") {
    auto lum = [](Rgba v) { return 0.2126 * v.r + 0.7152 * v.g + 0.0722 * v.b; };
    for (const ThemeKind theme : kThemes) {
        CAPTURE(static_cast<int>(theme));
        const Rgba base = color(theme, C::BgBase);
        const Rgba chrome = color(theme, C::BgChrome);
        const Rgba panel = color(theme, C::BgPanel);
        const Rgba raised = color(theme, C::BgRaised);
        const Rgba input = color(theme, C::BgInput);
        // The page is the deepest layer; the frame (title bar, ribbon band, status bar, navigation card) and
        // the content card sit on it.
        if (theme == ThemeKind::Light) {
            CHECK(lum(base) < lum(chrome));
            CHECK(lum(base) < lum(color(theme, C::BgTitleBar)));
            CHECK(lum(chrome) < lum(panel));
            CHECK(lum(raised) < lum(panel)); // a group on the white card is a shade grey
        } else {
            CHECK(lum(base) <= lum(color(theme, C::BgTitleBar)));
            CHECK(lum(base) < lum(chrome));
            CHECK(lum(chrome) < lum(panel));
            CHECK(lum(panel) < lum(raised));
            CHECK(lum(input) < lum(panel)); // an input is inset in the dark themes
            CHECK(lum(raised) < lum(color(theme, C::BgControl))); // a push button is lighter than its group
        }
        // An input reads as an input on the card and on a group: its border, and (dark) its own fill.
        for (const Rgba surface : {panel, raised, color(theme, C::BgOverlay)}) {
            CHECK(contrastRatio(color(theme, C::LineStrong), surface) >= 1.45);
            CHECK(contrastRatio(color(theme, C::LineStrong), surface) <= 2.6); // a line, not a frame
        }
        CHECK(contrastRatio(color(theme, C::LineSubtle), panel) >= 1.15);
        CHECK(contrastRatio(color(theme, C::LineStrong), input) >= 1.45);
        // Hover and selection: each visibly off the card, and off each other.
        for (int a = 0; a < kAccentCount; ++a) {
            setAccent(static_cast<Accent>(a));
            CAPTURE(a);
            const Rgba hover = over(color(theme, C::BgHover), panel);
            const Rgba selected = color(theme, C::AccentSubtle);
            CHECK(largestChannelStep(hover, panel) >= 8);
            CHECK(largestChannelStep(selected, panel) >= 8);
            CHECK(largestChannelStep(selected, hover) >= 6);
        }
        setAccent(Accent::Blue);
        // Disabled text: quieter than tertiary, still legible.
        CHECK(contrastRatio(color(theme, C::TextDisabled), panel) >= 2.2);
        CHECK(contrastRatio(color(theme, C::TextDisabled), panel) < contrastRatio(color(theme, C::TextTertiary), panel));
    }
}

TEST_CASE("D-137: high contrast keeps its own colours; its selection carries white text") {
    const ThemeKind hc = ThemeKind::HighContrast;
    for (int a = 0; a < kAccentCount; ++a) {
        setAccent(static_cast<Accent>(a));
        CHECK(colorArgb(hc, C::AccentBase) == tokens::kHighContrast[static_cast<std::size_t>(C::AccentBase)]);
        CHECK(contrastRatio(color(hc, C::TextPrimary), color(hc, C::AccentSubtle)) >= 7.0);
        CHECK(contrastRatio(color(hc, C::AccentText), color(hc, C::AccentSubtle)) >= 4.5);
    }
    setAccent(Accent::Blue);
    for (const C surface : {C::BgBase, C::BgPanel, C::BgInput, C::BgControl, C::BgChrome, C::BgTitleBar}) {
        CHECK(contrastRatio(color(hc, C::TextPrimary), color(hc, surface)) >= 7.0);
    }
}
