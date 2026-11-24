// 2026-11-10 UI polish of the shell chrome: the navigation's badges and motion, the status bar's
// segments, the window title. What the renders show, pinned.
#include "app/Localization.h"
#include "app/shell/ChromeText.h"
#include "app/shell/NavPanel.h"
#include "app/shell/PageView.h"
#include "app/shell/StatusBar.h"
#include "app/shell/TitleBar.h"
#include "app/state/AppState.h"
#include "support/TestGraphics.h"
#include "ui/anim/Tween.h"
#include "ui/render/OffscreenTarget.h"
#include "ui/widget/Host.h"

#include <doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::app;
using ui::PointF;
using ui::RectF;

namespace {

struct Harness {
    std::unique_ptr<ui::Host> host;

    explicit Harness(std::unique_ptr<ui::Widget> root, ui::SizeF size) {
        host = std::make_unique<ui::Host>(ui::HostServices{
            [] {},
            [](UINT, UINT) {},
            [](UINT) {},
            test::graphics().text.get(),
        });
        host->setRoot(std::move(root));
        host->layout(size);
    }
    void move(PointF p) { host->onPointer({ui::PointerAction::Move, p, ui::HitZone::Client}); }
    void click(PointF p) {
        move(p);
        host->onPointer({ui::PointerAction::Down, p, ui::HitZone::Client});
        host->onPointer({ui::PointerAction::Up, p, ui::HitZone::Client});
    }
};

// A root that places one child at `rect`.
class OneChild : public ui::Widget {
public:
    OneChild(std::unique_ptr<ui::Widget> child, RectF rect) : m_rect(rect) { addChild(std::move(child)); }
    void layout() override {
        if (!children().empty()) {
            children()[0]->setBounds(m_rect);
        }
    }

private:
    RectF m_rect;
};

// Motion as a user has it; Windows' own "Show animations" may still turn it off on the machine
// running the tests, so checks on a running animation are guarded with reducedMotion().
struct Motion {
    explicit Motion(bool instant) {
        ui::forceInstantMotion(instant);
        ui::setReducedMotionForced(false);
    }
    ~Motion() { ui::forceInstantMotion(false); }
    Motion(const Motion&) = delete;
    Motion& operator=(const Motion&) = delete;
};

double later() {
    return ui::nowMs() + 10000.0;
}

PointF centre(RectF r) {
    return {r.x + r.width / 2, r.y + r.height / 2};
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"shell-polish";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / name;
    std::filesystem::remove(file, ec);
    return file;
}

const Localization& turkish() {
    static const Localization strings = [] {
        std::ifstream in(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return Localization::fromJson(text.str()).value();
    }();
    return strings;
}

std::vector<NavPanel::Group> navGroups() {
    using ui::icons::Icon;
    return {{L"Kaldır", {{PageId::Components, L"Bileşenler", Icon::Add}}},
            {L"Yapılandır",
             {{PageId::Features, L"Özellikler", Icon::Add},
              {PageId::Tweaks, L"Ayarlar", Icon::Add},
              {PageId::Tasks, L"Zamanlanmış Görevler", Icon::Add},
              {PageId::Apply, L"Uygula", Icon::Add, /*counted=*/false}}}};
}

} // namespace

TEST_CASE("shell polish: a nav badge says 1, 2 and 3 digits as they are, more as 999+") {
    CHECK(NavPanel::badgeText(0).empty());
    CHECK(NavPanel::badgeText(-3).empty());
    CHECK(NavPanel::badgeText(7) == L"7");
    CHECK(NavPanel::badgeText(42) == L"42");
    CHECK(NavPanel::badgeText(318) == L"318");
    CHECK(NavPanel::badgeText(999) == L"999");
    CHECK(NavPanel::badgeText(1500) == L"999+");
}

TEST_CASE("shell polish: nav badges share one height and line; the labels start where they did") {
    Motion instant(true);
    auto panel = std::make_unique<NavPanel>();
    NavPanel* nav = panel.get();
    Harness h(std::make_unique<OneChild>(std::move(panel), RectF{16, 100, 200, 300}), {400, 500});
    nav->setGroups(navGroups());
    nav->setActive(PageId::Features);
    const RectF plainLabel = nav->labelRect(PageId::Tasks); // no count yet
    nav->setCount(PageId::Components, 7);
    nav->setCount(PageId::Features, 42);
    nav->setCount(PageId::Tweaks, 318);
    nav->setCount(PageId::Apply, 12); // not counted: no badge

    const RectF one = nav->badgeRect(PageId::Components);
    const RectF two = nav->badgeRect(PageId::Features);
    const RectF three = nav->badgeRect(PageId::Tweaks);
    CHECK(nav->badgeRect(PageId::Tasks).width == 0);
    CHECK(nav->badgeRect(PageId::Apply).width == 0);
    CHECK(one.height == ui::tokens::size::icon);
    CHECK(two.height == one.height);
    CHECK(three.height == one.height);
    CHECK(one.width >= one.height); // never narrower than tall
    CHECK(two.width > one.width);
    CHECK(three.width > two.width);
    // Right-aligned at the same edge, centred in their rows alike.
    CHECK(one.right() == doctest::Approx(two.right()));
    CHECK(two.right() == doctest::Approx(three.right()));
    for (const PageId page : {PageId::Components, PageId::Features, PageId::Tweaks}) {
        const RectF label = nav->labelRect(page);
        const RectF badge = nav->badgeRect(page);
        CAPTURE(static_cast<int>(page));
        CHECK(label.x == doctest::Approx(plainLabel.x)); // the text does not move for a badge
        CHECK(label.right() < badge.x);
        CHECK(badge.y - label.y == doctest::Approx(one.y - nav->labelRect(PageId::Components).y));
    }
    // Compact (Ctrl+B): icons only, no badges.
    nav->setCompact(true);
    CHECK(nav->badgeRect(PageId::Components).width == 0);
    nav->setCompact(false);

    // Drawn: the chosen item's badge is filled with the accent, the others only tinted with it.
    auto target = ui::OffscreenTarget::create(*test::graphics().device, {400, 500}, 1.0f);
    REQUIRE(target.has_value());
    auto& t = **target;
    {
        ui::Canvas canvas(t.beginDraw(), ui::ThemeKind::Dark, 1.0f, *test::graphics().text, *test::graphics().icons);
        canvas.clear(ui::tokens::Color::BgBase);
        h.host->paint(canvas);
    }
    REQUIRE(t.endDraw().has_value());
    (void)t.savePng(scratch(L"nav-badges.png")); // to look at
    const auto pixels = t.readPixels();
    REQUIRE(pixels.has_value());
    auto at = [&](RectF r) { // just inside the badge's left edge, on its middle line (no digit there)
        return (*pixels)[static_cast<std::size_t>(r.y + r.height / 2) * t.widthPx() + static_cast<std::size_t>(r.x + 2)];
    };
    const std::uint32_t accent = ui::colorArgb(ui::ThemeKind::Dark, ui::tokens::Color::AccentBase);
    const std::uint32_t card = ui::colorArgb(ui::ThemeKind::Dark, ui::tokens::Color::BgChrome);
    CHECK(at(two) == accent); // Özellikler is the chosen item
    CHECK(at(one) != accent);
    CHECK(at(one) != card);
}

TEST_CASE("shell polish: the nav's chosen fill slides to a clicked item and stops; hover fades and stops") {
    Motion motion(false);
    auto panel = std::make_unique<NavPanel>();
    NavPanel* nav = panel.get();
    Harness h(std::make_unique<OneChild>(std::move(panel), RectF{16, 100, 200, 300}), {400, 500});
    nav->setGroups(navGroups());
    nav->setActive(PageId::Components);
    std::vector<PageId> chosen;
    nav->onSelect = [&](PageId page) {
        chosen.push_back(page);
        nav->setActive(page); // what the shell does: the page it shows, the same item
    };

    h.click(centre(nav->labelRect(PageId::Tweaks)));
    REQUIRE(chosen.size() == 1);
    CHECK(chosen.back() == PageId::Tweaks);
    CHECK(nav->moving() == !ui::reducedMotion()); // the shell's own setActive kept the slide
    CHECK(nav->animating() == !ui::reducedMotion());
    CHECK_FALSE(nav->tick(later())); // it ends: no frames when idle
    CHECK_FALSE(nav->moving());
    CHECK_FALSE(nav->animating());

    // The keyboard chooses the same way.
    CHECK(nav->onKeyDown({VK_DOWN, false, false, false}));
    CHECK(chosen.back() == PageId::Tasks);
    CHECK(nav->moving() == !ui::reducedMotion());
    // Another page set by code (a shortcut, the palette) while it slides: there at once.
    nav->setActive(PageId::Components);
    CHECK_FALSE(nav->moving());

    h.move({500, 450}); // off the card: the hover fades out, then nothing runs
    CHECK_FALSE(nav->tick(later()));
    CHECK_FALSE(nav->animating());

    // --render / reduced motion: the click's result is simply there, as before.
    ui::forceInstantMotion(true);
    h.click(centre(nav->labelRect(PageId::Features)));
    CHECK(chosen.back() == PageId::Features);
    CHECK_FALSE(nav->moving());
    CHECK_FALSE(nav->animating());
}

TEST_CASE("shell polish: the compact navigation names the icon under the pointer, next to it") {
    Motion instant(true);
    auto panel = std::make_unique<NavPanel>();
    NavPanel* nav = panel.get();
    Harness h(std::make_unique<OneChild>(std::move(panel), RectF{16, 100, NavPanel::kCompactWidth, 300}), {400, 500});
    nav->setGroups(navGroups());
    nav->setCompact(true);
    // Rows: 4 high caption, then 28 per item; the second group after an 8 gap.
    const float first = 100 + 4 + ui::tokens::size::navItem / 2;
    h.move({38, first});
    CHECK(nav->tooltip() == L"Bileşenler");
    CHECK(nav->tooltipAnchor().y < first);
    CHECK(nav->tooltipAnchor().bottom() > first);
    const float second = 100 + 4 + ui::tokens::size::navItem + 8 + 4 + ui::tokens::size::navItem / 2;
    h.move({38, second});
    CHECK(nav->tooltip() == L"Özellikler"); // the same widget, another item: its own name
    CHECK(nav->tooltipAnchor().y > first);
    h.move({300, 450});
    CHECK(nav->tooltip().empty());
}

TEST_CASE("shell polish: the status bar draws a segment only when it has something to say") {
    Motion instant(true);
    auto widget = std::make_unique<StatusBar>();
    StatusBar* bar = widget.get();
    const RectF rect{0, 678, 1100, ui::tokens::size::statusBar};
    Harness h(std::make_unique<OneChild>(std::move(widget), rect), {1100, 700});
    using Segment = StatusBar::Segment;
    std::vector<Segment> clicked;
    bar->onSegment = [&](Segment s) { clicked.push_back(s); };

    bar->setModel({L"Bağlı imaj yok", false, L"", L"", L"", L"", L""});
    bar->setInfo({L"DISM 10.0.26100.1", L"Boş alan C: 182 GB"});
    CHECK(bar->segmentRect(Segment::Image).width > 0);
    CHECK(bar->segmentRect(Segment::Changes).width == 0); // no "0 değişiklik"
    CHECK(bar->segmentRect(Segment::Size).width == 0);
    CHECK(bar->infoRect().right() <= rect.right());

    bar->setModel({L"Windows 11 Pro", true, L"image tip", L"12 değişiklik", L"queue tip", L"≈ 1,2 GB kazanç", L"size tip"});
    const RectF image = bar->segmentRect(Segment::Image);
    const RectF changes = bar->segmentRect(Segment::Changes);
    const RectF size = bar->segmentRect(Segment::Size);
    CHECK(changes.width > 0);
    CHECK(size.width > 0);
    CHECK(image.right() < changes.x); // left to right, apart
    CHECK(changes.right() < size.x);
    CHECK(size.right() < bar->infoRect().x);
    for (const RectF r : {image, changes, size}) {
        CHECK(r.y >= rect.y);
        CHECK(r.bottom() <= rect.bottom());
    }

    // Each one is a link: hovering shows a hand and its own tooltip, a click says which.
    h.move(centre(changes));
    CHECK(bar->cursor() == ui::Cursor::Hand);
    CHECK(bar->tooltip() == L"queue tip");
    CHECK(bar->tooltipAnchor().x == doctest::Approx(changes.x)); // placed at the segment, not the bar's left
    h.move(centre(size));
    CHECK(bar->tooltip() == L"size tip"); // another segment under the same widget: its own text
    h.click(centre(changes));
    h.click(centre(image));
    CHECK(clicked == std::vector<Segment>{Segment::Changes, Segment::Image});

    // The queue emptied under the pointer: its segment goes, nothing stays hovered.
    h.move(centre(size));
    bar->setModel({L"Windows 11 Pro", true, L"image tip", L"", L"", L"", L""});
    CHECK(bar->segmentRect(Segment::Size).width == 0);
    CHECK(bar->cursor() == ui::Cursor::Arrow);
    CHECK(bar->tooltip().empty());
    CHECK_FALSE(bar->animating());
}

TEST_CASE("shell polish: the status model comes from the state") {
    const Localization& strings = turkish();
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    StatusBar::Model model = statusModel(state, strings, Language::Turkish);
    CHECK(model.image == strings.get(Str::StatusNoMount));
    CHECK_FALSE(model.mounted);
    CHECK(model.changes.empty());
    CHECK(model.size.empty());

    state.setMounted(MountedImage{LR"(C:\WinLove\mount)", LR"(D:\work\sources\install.wim)", 6, L"Windows 11 Pro"});
    model = statusModel(state, strings, Language::Turkish);
    CHECK(model.mounted);
    CHECK(model.image == L"Windows 11 Pro");
    CHECK(model.imageTip.find(LR"(C:\WinLove\mount)") != std::wstring::npos);
    CHECK(model.changes.empty());

    using core::ops::OpKind;
    using core::ops::Operation;
    state.queue(Operation{OpKind::DisableFeature, L"NetFx3"});
    model = statusModel(state, strings, Language::Turkish);
    CHECK(model.changes == strings.get(Str::StatusChangesOne));
    CHECK(model.size.empty()); // no estimate: nothing to say

    Operation removal{OpKind::RemoveAppx, L"Clipchamp"};
    removal.sizeDelta = -300LL * 1024 * 1024;
    state.queue(removal);
    Operation update{OpKind::AddPackage, LR"(D:\kb.msu)"};
    update.sizeDelta = 100LL * 1024 * 1024;
    state.queue(update);
    model = statusModel(state, strings, Language::Turkish);
    CHECK(model.changes == strings.format(Str::StatusChangesMany, {{L"n", L"3"}}));
    CHECK(model.size.find(L"200") != std::wstring::npos); // −300 MB + 100 MB
    CHECK(model.size.find(L"kazanç") != std::wstring::npos);
    CHECK_FALSE(model.sizeTip.empty());
}

TEST_CASE("shell polish: the window title is centred, cut before the buttons, never over the menus") {
    const RectF bar{0, 0, 1100, ui::tokens::size::titleBar};
    // Short: in the middle of the window.
    const RectF centred = TitleBar::titleRectFor(bar, 60, 300, 900);
    CHECK(centred.x == doctest::Approx(520));
    CHECK(centred.width == doctest::Approx(60));
    // Wider than the middle allows: off centre first, then cut at the right limit.
    const RectF moved = TitleBar::titleRectFor(bar, 500, 300, 900);
    CHECK(moved.x >= 300);
    CHECK(moved.right() <= 900);
    CHECK(moved.width == doctest::Approx(500));
    const RectF cut = TitleBar::titleRectFor(bar, 900, 300, 900);
    CHECK(cut.x == doctest::Approx(300));
    CHECK(cut.right() == doctest::Approx(900));
    // No room for a few letters: none.
    CHECK(TitleBar::titleRectFor(bar, 200, 300, 330).width == 0);

    // The real bar at the smallest window, with a very long name.
    TitleBar::Labels labels{L"WinLove", L"", L"Ctrl", L"", L"", L"", L"", L"", L"",
                            {L"Görüntü", L"Ön Ayar", L"Araçlar", L"Katılımsız"}};
    auto widget = std::make_unique<TitleBar>(labels);
    TitleBar* title = widget.get();
    Harness h(std::make_unique<OneChild>(std::move(widget), bar), {1100, 700});
    // D-147: the app's name only, as the wordmark (never the open image).
    CHECK(title->title() == L"WinLove");
    const RectF r = title->titleRect();
    REQUIRE(r.width > 0);
    CHECK(r.width < 100); // "WinLove" in the wordmark style, not a long image name
    float menusRight = 0;
    for (const auto& child : title->children()) {
        if (dynamic_cast<const MenuButton*>(child.get())) {
            menusRight = std::max(menusRight, child->bounds().right());
        }
    }
    CHECK(r.x > menusRight);
    CHECK(r.right() < title->themeButton().bounds().x);
    CHECK(std::abs(r.x + r.width / 2 - bar.width / 2) <= 2.0f); // centred in the window
}

TEST_CASE("shell polish: a page without actions has no ribbon band, and its card starts where every card does") {
    PageView with(L"Bileşenler", L"Açıklama");
    with.addAction(ui::ButtonKind::Ribbon, L"Yenile");
    PageView without(L"Hakkında", L"");
    with.setBounds({0, 34, 1100, 644});
    without.setBounds({0, 34, 1100, 644});
    CHECK(with.ribbonShown());
    CHECK_FALSE(without.ribbonShown());
    CHECK(with.contentTop() == without.contentTop()); // nothing moves from page to page
    CHECK(with.cardRect().y == without.cardRect().y);
}
