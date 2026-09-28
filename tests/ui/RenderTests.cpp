// Render-layer tests: path parsing, bundled fonts, and pixel-exact drawing on an offscreen target.
#include "ui/render/Canvas.h"
#include "ui/render/Graphics.h"
#include "ui/render/OffscreenTarget.h"
#include "ui/render/SvgPath.h"
#include "support/TestGraphics.h"

#include <doctest.h>

#include <string>
#include <vector>

using namespace wl;
using namespace wl::ui;

namespace {

using test::graphics;

D2D1_RECT_F bounds(ID2D1PathGeometry* geometry) {
    D2D1_RECT_F b{};
    geometry->GetBounds(nullptr, &b);
    return b;
}

std::uint32_t pixel(const std::vector<std::uint32_t>& pixels, UINT width, UINT x, UINT y) {
    return pixels[static_cast<std::size_t>(y) * width + x];
}

} // namespace

TEST_CASE("SVG path: absolute and relative lines, close") {
    auto g = buildSvgPath(graphics().device->d2dFactory(), "M2 4h12v9H2z");
    REQUIRE(g.has_value());
    const auto b = bounds(g->Get());
    CHECK(b.left == doctest::Approx(2));
    CHECK(b.top == doctest::Approx(4));
    CHECK(b.right == doctest::Approx(14));
    CHECK(b.bottom == doctest::Approx(13));
}

TEST_CASE("SVG path: arcs and compact numbers") {
    // Circle of radius 6 around (8,8) written as two arcs, with "0-12" compact syntax.
    auto g = buildSvgPath(graphics().device->d2dFactory(), "M8 2a6 6 0 1 0 0 12a6 6 0 1 0 0-12z");
    REQUIRE(g.has_value());
    const auto b = bounds(g->Get());
    CHECK(b.left == doctest::Approx(2).epsilon(0.01));
    CHECK(b.right == doctest::Approx(14).epsilon(0.01));
    CHECK(b.bottom == doctest::Approx(14).epsilon(0.01));
}

TEST_CASE("SVG path: rejects garbage") {
    CHECK_FALSE(buildSvgPath(graphics().device->d2dFactory(), "12 12").has_value());
    CHECK_FALSE(buildSvgPath(graphics().device->d2dFactory(), "M1 2 X3").has_value());
}

TEST_CASE("every generated icon parses") {
    auto& icons = *graphics().icons;
    for (std::size_t i = 0; i < icons::kIconCount; ++i) {
        CAPTURE(icons::kIcons[i].name);
        for (const auto variant : {IconVariant::Regular16, IconVariant::Filled16, IconVariant::Regular24}) {
            CHECK(icons.get(static_cast<icons::Icon>(i), variant).geometry != nullptr);
        }
    }
}

TEST_CASE("bundled fonts resolve by typographic family") {
    const auto& fonts = *graphics().fonts;
    CHECK(fonts.hasFamily(tokens::font::kUi));
    CHECK(fonts.hasFamily(tokens::font::kMono));
    // Medium (500) must not fall back to a synthesized bold of Regular.
    auto* collection = fonts.collection();
    UINT32 index = 0;
    BOOL exists = FALSE;
    REQUIRE(SUCCEEDED(collection->FindFamilyName(tokens::font::kUi, &index, &exists)));
    ComPtr<IDWriteFontFamily> family;
    REQUIRE(SUCCEEDED(collection->GetFontFamily(index, &family)));
    ComPtr<IDWriteFont> medium;
    REQUIRE(SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STRETCH_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, &medium)));
    CHECK(medium->GetWeight() == DWRITE_FONT_WEIGHT_MEDIUM);
    CHECK(medium->GetSimulations() == DWRITE_FONT_SIMULATIONS_NONE);
}

TEST_CASE("text measures and Turkish glyphs render") {
    const auto& text = *graphics().text;
    const float narrow = text.measure(L"iiii", tokens::TypeStyle::Body);
    const float wide = text.measure(L"WWWW", tokens::TypeStyle::Body);
    CHECK(narrow > 0);
    CHECK(wide > narrow);
    CHECK(text.measure(L"ğüşİöç", tokens::TypeStyle::Body) > 0);
}

TEST_CASE("canvas: token colors and 1px hairlines are pixel exact at 150%") {
    auto target = OffscreenTarget::create(*graphics().device, {40, 20}, 1.5f);
    REQUIRE(target.has_value());
    auto& t = **target;
    {
        Canvas canvas(t.beginDraw(), ThemeKind::Dark, 1.5f, *graphics().text, *graphics().icons);
        canvas.clear(tokens::Color::BgBase);
        canvas.fillRect({0, 0, 40, 10}, tokens::Color::BgPanel);
        canvas.hairlineH(0, 10, 40, tokens::Color::AccentBase);
    }
    REQUIRE(t.endDraw().has_value());
    auto pixels = t.readPixels();
    REQUIRE(pixels.has_value());
    const UINT w = t.widthPx();
    CHECK(w == 60);
    CHECK(pixel(*pixels, w, 5, 5) == 0xFF201E1Cu);   // bg.panel (y 0..15 px)
    CHECK(pixel(*pixels, w, 5, 15) == 0xFFD4905Au);  // hairline: exactly one physical row at y=15
    CHECK(pixel(*pixels, w, 5, 14) == 0xFF201E1Cu);
    CHECK(pixel(*pixels, w, 5, 16) == 0xFF1A1918u);  // bg.base below
}
