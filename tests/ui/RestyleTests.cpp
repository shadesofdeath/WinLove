// D-136 (2026-11-10): the new look — 13 px text, roomier rows, rounder corners, raised cards. What
// the restyle added as logic, pinned: tab strips that scroll when their tabs do not fit, form and
// list metrics that follow the tokens, the card shadow blurred once and kept, tables whose fixed
// columns give way to the name column, and the whole text of a cut cell or label on hover.
#include "app/pages/source/RecentList.h"
#include "app/shell/NavPanel.h"
#include "app/shell/PageView.h"
#include "support/TestGraphics.h"
#include "ui/anim/Tween.h"
#include "ui/render/Canvas.h"
#include "ui/render/OffscreenTarget.h"
#include "ui/widget/Host.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/InfoBar.h"
#include "ui/widgets/StripScroll.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <doctest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::ui;

namespace {

struct Harness {
    std::unique_ptr<Host> host;

    explicit Harness(std::unique_ptr<Widget> root, SizeF size) {
        host = std::make_unique<Host>(HostServices{
            [] {},
            [](UINT, UINT) {},
            [](UINT) {},
            test::graphics().text.get(),
        });
        host->setRoot(std::move(root));
        host->layout(size);
    }
    void move(PointF p) { host->onPointer({PointerAction::Move, p, HitZone::Client}); }
};

// A root that places one child at `rect`.
class OneChild : public Widget {
public:
    OneChild(std::unique_ptr<Widget> child, RectF rect) : m_rect(rect) { addChild(std::move(child)); }
    void layout() override {
        if (!children().empty()) {
            children()[0]->setBounds(m_rect);
        }
    }

private:
    RectF m_rect;
};

constexpr float lineOf(tokens::TypeStyle style) {
    return tokens::kTypeStyles[static_cast<std::size_t>(style)].lineHeight;
}

std::vector<std::wstring> manyTabs() {
    return {L"Gizlilik", L"Yapay zekâ", L"Uygulamalar", L"Performans", L"Görünüm",
            L"Gezgin", L"Başlat ve görev çubuğu", L"Güncelleme", L"Güvenlik", L"Sistem"};
}

std::uint32_t pixelAt(const std::vector<std::uint32_t>& pixels, UINT width, UINT x, UINT y) {
    return pixels[static_cast<std::size_t>(y) * width + x];
}

int channelDistance(std::uint32_t a, std::uint32_t b) {
    int most = 0;
    for (int shift : {0, 8, 16}) {
        most = std::max(most, std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF)));
    }
    return most;
}

} // namespace

TEST_CASE("D-136: the approved look's tokens") {
    CHECK(tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::Body)].size == 13.0f);
    CHECK(lineOf(tokens::TypeStyle::Body) == 18.0f);
    CHECK(tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::Section)].size == 14.0f);
    CHECK(tokens::kTypeStyles[static_cast<std::size_t>(tokens::TypeStyle::Section)].weight == 600);
    CHECK(tokens::size::control == 28.0f);
    CHECK(tokens::size::tableRow == 28.0f);
    CHECK(tokens::size::navItem == 32.0f);
    CHECK(tokens::radius::r3 == 10.0f);
    CHECK(tokens::size::contentPad == 20.0f);
    CHECK(tokens::size::panelPad == 12.0f);
    CHECK(tokens::elevation::card.size() == 2);
}

TEST_CASE("strip scroll: a row that fits never moves, has no chevron") {
    StripScroll strip;
    strip.setExtent(200, 300);
    CHECK_FALSE(strip.overflows());
    CHECK_FALSE(strip.reveal(150, 40));
    CHECK(strip.offset() == 0.0f);
    CHECK(strip.chevronAt(0) == 0);
    CHECK(strip.chevronAt(299) == 0);
    CHECK_FALSE(strip.scrollBy(50));
}

TEST_CASE("strip scroll: an item is brought whole into view, clear of the edges that still hide tabs") {
    StripScroll strip;
    strip.setExtent(1000, 300);
    REQUIRE(strip.overflows());
    CHECK(strip.maxOffset() == doctest::Approx(700));
    CHECK_FALSE(strip.hidesStart());
    CHECK(strip.hidesEnd());
    CHECK(strip.chevronAt(299) == 1); // the end's chevron, at the very edge
    CHECK(strip.chevronAt(0) == 0);   // nothing hidden at the start: no chevron there

    // An item in the middle, to the right of the view: its end lands a whole edge before the view's end.
    CHECK(strip.reveal(400, 80));
    CHECK(strip.offset() + 300 - StripScroll::kEdge >= 480 - 0.5f);
    CHECK(strip.hidesStart());
    CHECK(strip.chevronAt(1) == -1);
    // The last item: the row's end, no room kept after it (nothing hidden there any more).
    CHECK(strip.reveal(920, 80));
    CHECK(strip.offset() == doctest::Approx(700));
    CHECK_FALSE(strip.hidesEnd());
    // The first item: back to the start.
    CHECK(strip.reveal(0, 80));
    CHECK(strip.offset() == 0.0f);
    // A chevron pages towards its end; the offset stays in range.
    CHECK(strip.page(1));
    CHECK(strip.offset() > 0.0f);
    strip.scrollBy(10000);
    CHECK(strip.offset() == doctest::Approx(700));
    // A narrower view keeps the offset in range.
    strip.setExtent(1000, 900);
    CHECK(strip.offset() == doctest::Approx(100));
}

TEST_CASE("tab bar: tabs wider than the bar scroll; the chosen tab is always whole in view") {
    ui::forceInstantMotion(true);
    auto widget = std::make_unique<TabBar>(manyTabs(), 0);
    TabBar* bar = widget.get();
    const RectF rect{10, 10, 320, tokens::size::control};
    Harness h(std::make_unique<OneChild>(std::move(widget), rect), {400, 100});
    REQUIRE(bar->strip().overflows());
    CHECK(bar->strip().offset() == 0.0f);

    bar->setSelected(9); // by code: the last tab
    const RectF last = bar->tabRect(9);
    CHECK(last.x >= rect.x - 0.5f);
    CHECK(last.right() <= rect.right() + 0.5f);

    // ← from the last tab, one at a time: each chosen tab stays whole in view.
    for (int i = 8; i >= 0; --i) {
        CHECK(bar->onKeyDown({VK_LEFT, false, false, false}));
        REQUIRE(bar->selected() == i);
        const RectF tab = bar->tabRect(i);
        CHECK(tab.x >= rect.x - 0.5f);
        CHECK(tab.right() <= rect.right() + 0.5f);
    }
    CHECK(bar->strip().offset() == 0.0f);

    // The wheel scrolls the row (a bar that fits would leave it to the page).
    CHECK(bar->onWheel({rect.x + 50, rect.y + 5}, -1.0f));
    CHECK(bar->strip().offset() > 0.0f);
    // A click on the end's chevron scrolls and chooses nothing.
    const float before = bar->strip().offset();
    bar->onPointerDown({rect.right() - 2, rect.y + 5});
    CHECK(bar->selected() == 0);
    CHECK(bar->strip().offset() >= before);
    ui::forceInstantMotion(false);
}

TEST_CASE("tab bar: a bar that fits is laid out exactly as before (no offset, no clip)") {
    auto widget = std::make_unique<TabBar>(std::vector<std::wstring>{L"Gizlilik", L"Yapay zekâ"}, 1);
    TabBar* bar = widget.get();
    Harness h(std::make_unique<OneChild>(std::move(widget), RectF{0, 0, 600, tokens::size::control}), {600, 100});
    CHECK_FALSE(bar->strip().overflows());
    CHECK(bar->tabRect(0).x == 0.0f);
    CHECK_FALSE(bar->onWheel({10, 10}, -1.0f));
}

TEST_CASE("card tabs: the page card's tabs scroll too, the chosen one in view") {
    auto widget = std::make_unique<app::CardTabs>();
    app::CardTabs* tabs = widget.get();
    Harness h(std::make_unique<OneChild>(std::move(widget), RectF{0, 0, 300, tokens::size::tab}), {400, 100});
    tabs->setTabs(manyTabs(), 9);
    REQUIRE(tabs->strip().overflows());
    CHECK(tabs->strip().offset() == doctest::Approx(tabs->strip().maxOffset()));
    tabs->setTabs(manyTabs(), 0);
    CHECK(tabs->strip().offset() == 0.0f);
}

TEST_CASE("form view: rows, controls and section titles follow the tokens") {
    CHECK(FormView::kControl == tokens::size::control);
    CHECK(FormView::kRow == tokens::size::control + 2 * tokens::spacing::s3);
    CHECK(FormView::kSectionLine == lineOf(tokens::TypeStyle::Section));
    CHECK(FormView::kSection == tokens::spacing::s7 + lineOf(tokens::TypeStyle::Section) + tokens::spacing::s4);

    auto view = std::make_unique<FormView>(200.0f);
    FormView* form = view.get();
    const RectF rect{0, 0, 600, 400};
    Harness h(std::make_unique<OneChild>(std::move(view), rect), {600, 400});
    form->addSection(L"Görünüm");
    auto& first = form->addRow<Toggle>(L"Hareketi azalt", L"", 0.0f, L"", false);
    auto& second = form->addRow<Toggle>(L"Bir şey daha", L"", 0.0f, L"", false);
    CHECK(first.bounds().height == FormView::kControl);
    CHECK(first.bounds().y == doctest::Approx(FormView::kSection + (FormView::kRow - FormView::kControl) / 2));
    CHECK(second.bounds().y - first.bounds().y == doctest::Approx(FormView::kRow));
}

TEST_CASE("form view: a label cut to an ellipsis shows whole on hover, a short one does not") {
    auto view = std::make_unique<FormView>(120.0f); // a label room of 120 - 12 - 16 = 92
    FormView* form = view.get();
    Harness h(std::make_unique<OneChild>(std::move(view), RectF{0, 0, 600, 400}), {600, 400});
    form->addSection(L"Bölüm");
    form->addRow<Toggle>(L"Bulutun eklediği sabitlemeler ve içerik", L"", 0.0f, L"", false);
    form->addRow<Toggle>(L"Kısa", L"", 0.0f, L"", false);
    const float firstRow = FormView::kSection + FormView::kRow / 2;
    h.move({30, firstRow});
    CHECK(form->tooltip() == L"Bulutun eklediği sabitlemeler ve içerik");
    CHECK(form->tooltipAnchor().y <= firstRow);
    h.move({30, firstRow + FormView::kRow});
    CHECK(form->tooltip().empty());
}

TEST_CASE("table: fixed columns with a least width give way to the name column at a narrow table") {
    auto widget = std::make_unique<TableView>(std::vector<TableColumn>{
        {L"Ad", 0},
        {.title = L"Sağlayıcı", .width = 200, .minWidth = 100},
        {.title = L"Sürüm", .width = 100, .minWidth = 60},
        {L"Boyut", 80},
    });
    TableView* table = widget.get();
    Harness h(std::make_unique<OneChild>(std::move(widget), RectF{0, 0, 500, 300}), {800, 300});
    // 500 wide: the fixed ones take 380, the name 120.
    auto edges = table->columnEdges();
    CHECK(edges[1] - edges[0] == doctest::Approx(120));
    table->setFlexMin(220);
    edges = table->columnEdges();
    // The name wants 100 more; the two shrinkable columns can give 100 + 40 and give it in proportion.
    CHECK(edges[1] - edges[0] == doctest::Approx(220));
    CHECK(edges[2] - edges[1] == doctest::Approx(200 - 100.0f * 100 / 140));
    CHECK(edges[3] - edges[2] == doctest::Approx(100 - 100.0f * 40 / 140));
    CHECK(edges[4] - edges[3] == doctest::Approx(80)); // no least width: it keeps its width
    CHECK(edges.back() == doctest::Approx(500));
    // Wide enough: nothing shrinks.
    table->setBounds({0, 0, 800, 300});
    edges = table->columnEdges();
    CHECK(edges[2] - edges[1] == doctest::Approx(200));
    CHECK(edges[1] - edges[0] == doctest::Approx(420));
}

TEST_CASE("table: a cell whose text is cut shows it whole as the tooltip") {
    auto widget = std::make_unique<TableView>(std::vector<TableColumn>{{L"Ad", 120}, {L"Not", 0}});
    TableView* table = widget.get();
    Harness h(std::make_unique<OneChild>(std::move(widget), RectF{0, 0, 400, 200}), {400, 200});
    const std::wstring longName = L"Bağlı Kullanıcı Deneyimleri ve Telemetri";
    table->cellText = [&](int row, int column) -> TableCellText {
        if (column != 0) {
            return {};
        }
        return {row == 0 ? longName : L"Kısa"};
    };
    table->setRowCount(2);
    const float y0 = TableView::kHeader + TableView::kRow / 2;
    h.move({20, y0});
    CHECK(table->tooltip() == longName);
    h.move({20, y0 + TableView::kRow});
    CHECK(table->tooltip().empty());
}

TEST_CASE("list metrics: the recent list's two lines, the info bar and the nav fill follow the tokens") {
    CHECK(app::RecentList::kRow == lineOf(tokens::TypeStyle::Body) + lineOf(tokens::TypeStyle::Caption) + 2 * tokens::spacing::s3);
    CHECK(InfoBar::kHeight == tokens::size::control + 2 * tokens::spacing::s2);

    auto panel = std::make_unique<app::NavPanel>();
    app::NavPanel* nav = panel.get();
    Harness h(std::make_unique<OneChild>(std::move(panel), RectF{16, 100, 200, 400}), {400, 600});
    nav->setGroups({{L"Kaldır", {{app::PageId::Components, L"Bileşenler", icons::Icon::Add}}}});
    nav->setActive(app::PageId::Components);
    // The chosen fill: the item's height less a spacing step (it was a fixed 23 in a 28 item).
    CHECK(nav->focusRect().height == doctest::Approx(tokens::size::navItem - tokens::spacing::s2));
}

TEST_CASE("card shadow: blurred once per theme and scale, drawn in nine pieces as a direct blur would be") {
    const float scale = 1.5f;
    auto draw = [&](RectF card, bool cached) -> std::vector<std::uint32_t> {
        auto target = OffscreenTarget::create(*test::graphics().device, {200, 140}, scale);
        REQUIRE(target.has_value());
        auto& t = **target;
        {
            Canvas canvas(t.beginDraw(), ThemeKind::Light, scale, *test::graphics().text, *test::graphics().icons);
            canvas.clear(tokens::Color::BgBase);
            if (cached) {
                canvas.cardShadow(card, tokens::radius::r3);
            } else {
                canvas.dropShadow(card, tokens::radius::r3, tokens::elevation::card);
            }
            canvas.fillRoundRect(card, tokens::radius::r3, tokens::Color::BgPanel);
        }
        REQUIRE(t.endDraw().has_value());
        auto pixels = t.readPixels();
        REQUIRE(pixels.has_value());
        return *pixels;
    };
    const RectF card{20, 20, 140, 80}; // on the pixel grid at 150 %
    const std::size_t kept = Canvas::cardShadowsKept();
    const auto nine = draw(card, true);
    const std::size_t afterFirst = Canvas::cardShadowsKept();
    CHECK(afterFirst <= kept + 1);
    draw({30, 26, 120, 64}, true); // another size: the same bitmap
    CHECK(Canvas::cardShadowsKept() == afterFirst);

    const auto direct = draw(card, false);
    const UINT w = static_cast<UINT>(200 * scale);
    const std::uint32_t page = colorArgb(ThemeKind::Light, tokens::Color::BgBase);
    // Under the card (the key and ambient layers fall below it): darker than the page.
    const UINT below = static_cast<UINT>((card.bottom() + 3) * scale);
    const UINT middle = static_cast<UINT>(card.center().x * scale);
    CHECK(channelDistance(pixelAt(nine, w, middle, below), page) > 2);
    // The nine pieces match the blur they were cut from: under, beside, at a corner, far away.
    for (const auto [x, y] : {std::pair{middle, below}, std::pair{static_cast<UINT>((card.right() + 4) * scale), middle / 2},
                              std::pair{static_cast<UINT>((card.right() + 2) * scale), static_cast<UINT>((card.bottom() + 2) * scale)},
                              std::pair{static_cast<UINT>(5 * scale), static_cast<UINT>(130 * scale)}}) {
        CAPTURE(x);
        CAPTURE(y);
        CHECK(channelDistance(pixelAt(nine, w, x, y), pixelAt(direct, w, x, y)) <= 3);
    }
}
