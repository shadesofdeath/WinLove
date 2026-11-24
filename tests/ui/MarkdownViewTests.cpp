// D-145: the Markdown view — sections laid out for the width (a run of text per paragraph, item, cell),
// the link under a point and its click (an address goes out, "#anchor" scrolls), the selection and its
// copy, scrolling, a narrow width, an empty view; painted offscreen without a window.
#include "ui/render/Canvas.h"
#include "ui/render/OffscreenTarget.h"
#include "ui/widget/Host.h"
#include "ui/widgets/MarkdownView.h"
#include "support/TestGraphics.h"

#include <doctest.h>

#include <algorithm>

using namespace wl;
using namespace wl::ui;

namespace {

struct Hosted {
    std::unique_ptr<Host> host;
    MarkdownView* view = nullptr;
    explicit Hosted(SizeF size) {
        host = std::make_unique<Host>(HostServices{[] {}, [](UINT, UINT) {}, [](UINT) {}, test::graphics().text.get()});
        auto owned = std::make_unique<MarkdownView>();
        view = owned.get();
        host->setRoot(std::move(owned));
        host->layout(size);
    }
};

MarkdownView::Section section(std::wstring title, std::wstring_view body, std::wstring divider = {}) {
    MarkdownView::Section s;
    s.title = std::move(title);
    s.meta = L"v1.0 · today";
    s.body = markdown::parse(body);
    s.emptyBody = L"(no notes)";
    s.divider = std::move(divider);
    return s;
}

std::optional<std::size_t> runWith(const MarkdownView& view, std::wstring_view text) {
    for (std::size_t i = 0; i < view.runCount(); ++i) {
        if (view.runText(i).find(text) != std::wstring::npos) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("D-145: Markdown view — laid out, links, anchors, selection, scrolling") {
    Hosted h({700, 400});
    MarkdownView& view = *h.view;
    std::vector<std::wstring> opened;
    view.onLink = [&](const std::wstring& url) { opened.push_back(url); };
    std::wstring body = L"Intro with a [link](https://example.com/a) and `code`.\n\n[Jump](#later)\n\n";
    for (int i = 0; i < 30; ++i) {
        body += L"- item " + std::to_wstring(i) + L"\n";
    }
    body += L"\n## Later\n\n| a | b |\n|---|--:|\n| 1 | 2 |\n\n```\ncode block\n```\n\n> quoted\n";
    view.setSections({section(L"Release 2", body), section(L"Release 1", L"", L"Older")});
    REQUIRE(view.runCount() > 30);
    CHECK(view.contentHeight() > 400);
    for (const wchar_t* text : {L"Release 2", L"Intro with a link and code.", L"item 29", L"Later", L"code block", L"quoted", L"Older",
                                L"(no notes)"}) {
        CHECK_MESSAGE(runWith(view, text), text);
    }

    // The link: under its text a click opens it; beside it nothing.
    const auto intro = *runWith(view, L"Intro");
    const RectF r = view.runRect(intro);
    std::optional<std::wstring> found;
    float linkX = 0;
    for (float x = r.x; x < r.right() && !found; x += 2) {
        found = view.linkAt({x, r.y + 9});
        linkX = x;
    }
    REQUIRE(found);
    CHECK(*found == L"https://example.com/a");
    view.onPointerDown({linkX + 1, r.y + 9});
    view.onPointerUp({linkX + 1, r.y + 9});
    REQUIRE(opened.size() == 1);
    CHECK(opened.front() == L"https://example.com/a");
    CHECK_FALSE(view.linkAt({r.x + 2, r.y + 9})); // "Intro"

    // "#later" scrolls to the heading instead of leaving the window.
    const auto jump = *runWith(view, L"Jump");
    const RectF j = view.runRect(jump);
    REQUIRE(view.linkAt({j.x + 4, j.y + 9}) == std::optional<std::wstring>(L"#later"));
    CHECK(view.scrollOffset() == 0);
    view.onPointerDown({j.x + 4, j.y + 9});
    view.onPointerUp({j.x + 4, j.y + 9});
    CHECK(opened.size() == 1);
    CHECK(view.scrollOffset() > 0);
    const RectF later = view.runRect(*runWith(view, L"Later"));
    CHECK(later.y < 100); // the heading near the top

    // Selection: all of it, then a drag over the intro.
    view.selectAll();
    const std::wstring all = view.selectedText();
    CHECK(all.find(L"Release 2") != std::wstring::npos);
    CHECK(all.find(L"item 7") != std::wstring::npos);
    CHECK(all.find(L"•") == std::wstring::npos); // the bullets are marks, not text
    view.clearSelection();
    CHECK(view.selectedText().empty());
    view.scrollTo(0);
    const RectF i = view.runRect(intro);
    view.onPointerDown({i.x + 1, i.y + 9});
    // A drag needs the press captured, as the host does it.
    h.host->onPointer({PointerAction::Move, {i.x + 1, i.y + 9}, HitZone::Client});
    h.host->onPointer({PointerAction::Down, {i.x + 1, i.y + 9}, HitZone::Client});
    h.host->onPointer({PointerAction::Move, {i.x + 60, i.y + 9}, HitZone::Client});
    h.host->onPointer({PointerAction::Up, {i.x + 60, i.y + 9}, HitZone::Client});
    const std::wstring part = view.selectedText();
    CHECK(part.starts_with(L"Intro"));
    CHECK(part.size() < 20);

    // Keys: Ctrl+A selects all, End / Home scroll.
    CHECK(view.onKeyDown({'A', true, false, false}));
    CHECK(view.selectedText() == all);
    CHECK(view.onKeyDown({VK_END, false, false, false}));
    CHECK(view.scrollOffset() == doctest::Approx(view.contentHeight() - 400));
    CHECK(view.onKeyDown({VK_HOME, false, false, false}));
    CHECK(view.scrollOffset() == 0);
    CHECK(view.onWheel({10, 10}, -3));
    CHECK(view.scrollOffset() > 0);

    // A narrow window: more lines, still everything.
    h.host->layout({240, 400});
    CHECK(view.contentHeight() > 400);
    CHECK(runWith(view, L"item 29"));

    // Drawn (dark and light) without a window.
    auto target = OffscreenTarget::create(*test::graphics().device, {700, 400}, 1.5f);
    REQUIRE(target);
    for (const ThemeKind theme : {ThemeKind::Dark, ThemeKind::Light}) {
        h.host->layout({700, 400});
        Canvas canvas((*target)->beginDraw(), theme, 1.5f, *test::graphics().text, *test::graphics().icons);
        canvas.clear(tokens::Color::BgBase);
        h.host->paint(canvas);
        CHECK((*target)->endDraw());
    }

    // Nothing to show: the placeholder, no runs.
    view.setSections({});
    view.setPlaceholder(L"nothing");
    CHECK(view.runCount() == 0);
    CHECK(view.selectedText().empty());
}
