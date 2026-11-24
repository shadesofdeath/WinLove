// 2026-11-10 UI polish of the Source page: the drop target as one row, the recent sources as a
// compact list that scrolls in its room, the x that takes an entry whose file is gone off the list
// (and the list file), the quiet empty list. What the renders show, pinned.
#include "app/Localization.h"
#include "app/pages/SourcePage.h"
#include "app/state/AppState.h"
#include "base/Utf8.h"
#include "support/TestGraphics.h"
#include "ui/widget/Host.h"

#include <doctest.h>
#include <json.hpp>

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

const Localization& turkish() {
    static const Localization strings = [] {
        std::ifstream in(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/tr.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return Localization::fromJson(text.str()).value();
    }();
    return strings;
}

std::filesystem::path scratchDir() {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"source-polish";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto file = scratchDir() / name;
    std::error_code ec;
    std::filesystem::remove(file, ec);
    return file;
}

// A recent.json with these paths, newest first.
void writeRecent(const std::filesystem::path& file, const std::vector<std::filesystem::path>& paths) {
    nlohmann::json sources = nlohmann::json::array();
    std::int64_t when = 1790609559;
    for (const auto& p : paths) {
        sources.push_back({{"path", utf8::fromWide(p.wstring())},
                           {"format", p.extension() == L".iso" ? "ISO" : "WIM"},
                           {"summary", "11 25H2 \xC2\xB7 26200.9445"},
                           {"size", 7217280579ull},
                           {"lastOpened", when}});
        when -= 86400;
    }
    std::ofstream(file, std::ios::binary) << nlohmann::json{{"version", 1}, {"sources", sources}}.dump(2);
}

PointF centre(RectF r) {
    return {r.x + r.width / 2, r.y + r.height / 2};
}

// The page as the Shell builds it, with the Shell's intents for a user's own file: removing takes the
// entry off the list (AppState::forgetRecent saves the file), opening is recorded.
struct Page {
    AppState state;
    std::vector<std::filesystem::path> opened;
    int picks = 0;
    std::unique_ptr<ui::Host> host;
    SourcePage* page = nullptr;

    Page(const std::filesystem::path& recentFile, RectF rect)
        : state(recentFile, scratch(L"settings.json")) {
        SourcePage::Intents intents;
        intents.pickFile = [this] { ++picks; };
        intents.openPath = [this](const std::filesystem::path& p) { opened.push_back(p); };
        intents.removePath = [this](const std::filesystem::path& p) { state.forgetRecent(p); };
        auto body = std::make_unique<SourcePage>(state, turkish(), Language::Turkish, std::move(intents));
        page = body.get();
        host = std::make_unique<ui::Host>(ui::HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, test::graphics().text.get()});
        host->setRoot(std::make_unique<OneChild>(std::move(body), rect));
        host->layout({rect.right() + 20, rect.bottom() + 20});
    }
    RecentList& list() { return page->recentList(); }
    void move(PointF p) { host->onPointer({ui::PointerAction::Move, p, ui::HitZone::Client}); }
    void click(PointF p) {
        move(p);
        host->onPointer({ui::PointerAction::Down, p, ui::HitZone::Client});
        host->onPointer({ui::PointerAction::Up, p, ui::HitZone::Client});
    }
    void key(UINT vk) { host->onKeyDown({vk, false, false, false}); }
};

} // namespace

TEST_CASE("source polish: the x on an entry whose file is gone takes it off the list and the list file") {
    const auto kept = scratchDir() / L"kept.iso";
    const auto kept2 = scratchDir() / L"kept2.wim";
    const auto gone = scratchDir() / L"gone.iso";
    std::ofstream(kept) << "x";
    std::ofstream(kept2) << "x";
    std::error_code ec;
    std::filesystem::remove(gone, ec);
    const auto file = scratch(L"recent-x.json");
    writeRecent(file, {kept, gone, kept2});

    Page p(file, {0, 0, 900, 600});
    RecentList& list = p.list();
    REQUIRE(list.count() == 3);
    CHECK_FALSE(list.missing(0));
    CHECK(list.missing(1));
    // At rest only the missing entry shows its x; a row under the pointer shows its own.
    CHECK(list.removeShown(1));
    CHECK_FALSE(list.removeShown(0));
    CHECK_FALSE(list.removeShown(2));
    p.move(centre(list.rowRect(0)));
    CHECK(list.removeShown(0));
    // The x sits inside its row, at its end.
    CHECK(list.rowRect(1).contains(centre(list.removeRect(1))));
    CHECK(list.removeRect(1).right() <= list.rowRect(1).right());

    p.click(centre(list.removeRect(1)));
    CHECK(p.opened.empty()); // the x does not open the entry
    REQUIRE(p.state.recent().entries().size() == 2);
    CHECK(list.count() == 2);
    CHECK_FALSE(list.missing(0));
    CHECK_FALSE(list.missing(1));

    RecentSources reloaded(file);
    reloaded.load();
    REQUIRE(reloaded.entries().size() == 2);
    CHECK(reloaded.entries()[0].path == kept);
    CHECK(reloaded.entries()[1].path == kept2);
    std::filesystem::remove(file, ec);
}

TEST_CASE("source polish: a row opens with one click; the keyboard reaches the x with Right, Delete removes") {
    const auto a = scratchDir() / L"a.iso";
    const auto b = scratchDir() / L"b.wim";
    const auto c = scratchDir() / L"c.wim";
    for (const auto& f : {a, b, c}) {
        std::ofstream(f) << "x";
    }
    const auto file = scratch(L"recent-keys.json");
    writeRecent(file, {a, b, c});
    Page p(file, {0, 0, 900, 600});
    RecentList& list = p.list();
    REQUIRE(list.count() == 3);

    // One click on a row (not its x) opens it, as the cards did.
    p.click({list.rowRect(1).x + 60, centre(list.rowRect(1)).y});
    REQUIRE(p.opened.size() == 1);
    CHECK(p.opened.front() == b);
    CHECK(p.state.recent().entries().size() == 3);

    p.host->setFocus(&list, /*visible=*/true);
    p.key(VK_HOME);
    p.key(VK_RETURN); // Enter opens the selected row
    REQUIRE(p.opened.size() == 2);
    CHECK(p.opened.back() == a);

    // → puts the keyboard on the row's x: the ring goes there, Enter removes instead of opening.
    p.key(VK_DOWN);
    p.key(VK_RIGHT);
    CHECK(list.removeFocused());
    CHECK(list.focusRect().width == doctest::Approx(list.removeRect(1).width));
    p.key(VK_LEFT);
    CHECK_FALSE(list.removeFocused());
    p.key(VK_RIGHT);
    p.key(VK_RETURN);
    CHECK(p.opened.size() == 2);
    REQUIRE(p.state.recent().entries().size() == 2);
    CHECK(p.state.recent().entries()[1].path == c);

    // Delete removes the selected row at once.
    p.host->setFocus(&list, /*visible=*/true);
    p.key(VK_HOME);
    p.key(VK_DELETE);
    REQUIRE(p.state.recent().entries().size() == 1);
    CHECK(p.state.recent().entries()[0].path == c);
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST_CASE("source polish: rows are hit where they are drawn, the list scrolls in its room") {
    std::vector<std::filesystem::path> paths;
    for (int i = 0; i < 10; ++i) {
        paths.push_back(scratchDir() / (L"scroll" + std::to_wstring(i) + L".wim"));
        std::ofstream(paths.back()) << "x";
    }
    const auto file = scratch(L"recent-scroll.json");
    writeRecent(file, paths);
    // Room for the drop row, the section's title and a few rows: the list scrolls, the page does not.
    Page p(file, {0, 0, 900, 320});
    RecentList& list = p.list();
    REQUIRE(list.count() == 10);
    const RectF b = list.bounds();
    CHECK(b.bottom() == doctest::Approx(320.0f));
    CHECK(list.contentHeight() == doctest::Approx(10 * RecentList::kRow));
    CHECK(list.maxScroll() == doctest::Approx(10 * RecentList::kRow - b.height));
    CHECK(p.page->contentHeight() <= 320.0f + 0.5f);

    for (int i = 0; i < 3; ++i) {
        CHECK(list.rowAt(centre(list.rowRect(i))) == i);
        CHECK(list.rowRect(i).height == doctest::Approx(RecentList::kRow));
    }
    CHECK(list.rowAt({b.x + 10, b.y - 1}) == -1);
    CHECK(list.rowAt({b.right() + 1, b.y + 10}) == -1);
    // A scrolling list keeps the x off its scroll bar.
    CHECK(list.removeRect(0).right() <= b.right() - ui::ScrollBar::kWidth);

    // The wheel scrolls by rows; End brings the last row in, whole.
    CHECK(list.onWheel(centre(b), -1.0f));
    CHECK(list.scrollOffset() == doctest::Approx(RecentList::kRow));
    CHECK(list.rowAt(centre(b)) >= 1);
    p.host->setFocus(&list, /*visible=*/true);
    p.key(VK_END);
    CHECK(list.selected() == 9);
    CHECK(list.scrollOffset() == doctest::Approx(list.maxScroll()));
    CHECK(list.rowRect(9).bottom() <= b.bottom() + 0.5f);
    p.key(VK_HOME);
    CHECK(list.scrollOffset() == doctest::Approx(0.0f));

    // Not even a few rows fit: the list keeps three, the page reports its height to scroll instead.
    Page tight(file, {0, 0, 900, 150});
    CHECK(tight.list().bounds().height == doctest::Approx(3 * RecentList::kRow));
    CHECK(tight.page->contentHeight() > 150.0f);
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

TEST_CASE("source polish: the empty list keeps one quiet row, takes no focus; the drop row opens the picker") {
    const auto file = scratch(L"recent-empty.json");
    Page p(file, {0, 0, 900, 600});
    RecentList& list = p.list();
    CHECK(list.empty());
    CHECK(list.count() == 0);
    CHECK(list.visible());
    CHECK_FALSE(list.canFocus());
    CHECK(list.bounds().height == doctest::Approx(RecentList::kRow));
    CHECK(list.rowAt(centre(list.bounds())) == -1);

    DropRow& drop = p.page->dropRow();
    CHECK(drop.bounds().height == doctest::Approx(DropRow::kHeight));
    p.click(centre(drop.bounds()));
    CHECK(p.picks == 1);
    p.page->setDragState(ui::DropZone::DragState::Invalid);
    CHECK(drop.dragState() == ui::DropZone::DragState::Invalid);
    p.page->setLoading(true);
    p.click(centre(drop.bounds()));
    CHECK(p.picks == 1); // not while a source is opening
    p.page->setLoading(false);
    p.page->setDragState(ui::DropZone::DragState::None);
    p.host->setFocus(&drop, /*visible=*/true);
    p.key(VK_RETURN);
    CHECK(p.picks == 2);
}
