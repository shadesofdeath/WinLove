// docs/AUDIT-2026-11-10.md, the base + ui part: the behaviour its fixes changed, pinned.
#include "base/File.h"
#include "base/Log.h"
#include "support/TestGraphics.h"
#include "ui/anim/ItemMotion.h"
#include "ui/widget/Host.h"
#include "ui/widgets/Checkbox.h"
#include "ui/widgets/FormView.h"
#include "ui/widgets/LogConsole.h"
#include "ui/widgets/PillGroup.h"
#include "ui/widgets/SearchBox.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/TableView.h"
#include "ui/widgets/Toggle.h"

#include <doctest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace wl;
using namespace wl::ui;

namespace {

struct Harness {
    std::unique_ptr<Host> host;

    explicit Harness(std::unique_ptr<Widget> root, SizeF size = {400, 200}) {
        host = std::make_unique<Host>(HostServices{
            [] {},
            [](UINT, UINT) {},
            [](UINT) {},
            test::graphics().text.get(),
        });
        host->setRoot(std::move(root));
        host->layout(size);
    }
    void move(float x, float y) { host->onPointer({PointerAction::Move, {x, y}, HitZone::Client}); }
    void down(float x, float y) { host->onPointer({PointerAction::Down, {x, y}, HitZone::Client}); }
    void up(float x, float y) { host->onPointer({PointerAction::Up, {x, y}, HitZone::Client}); }
};

// A plain root that places one child at `bounds`.
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

// What a drag's end callback does when it rebuilds the page the dragged widget lives in.
class GoneOnRelease : public Widget {
public:
    explicit GoneOnRelease(int& clicks) : m_clicks(clicks) {}
    void onPointerUp(PointF) override { parent()->removeChild(this); }
    void onClick() override { ++m_clicks; }

private:
    int& m_clicks;
};

// A drag callback that locks the dragged control.
class LockedOnDrag : public Widget {
public:
    void onPointerMove(PointF) override {
        ++moves;
        setEnabled(false);
    }
    int moves = 0;
};

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"ui-audit";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / name;
}

std::vector<LogLine> numbered(int count) {
    std::vector<LogLine> lines;
    for (int i = 0; i < count; ++i) {
        lines.push_back(LogLine{L"12:00:00", LogLevel::Info, L"app", std::to_wstring(i)});
    }
    return lines;
}

// Motion as a user has it (not --render's instant motion); Windows' own "Show animations" may
// still turn it off on the machine running the tests, so checks on a running fade are guarded.
struct Motion {
    explicit Motion(bool instant) {
        forceInstantMotion(instant);
        setReducedMotionForced(false);
    }
    ~Motion() { forceInstantMotion(false); }
    Motion(const Motion&) = delete;
    Motion& operator=(const Motion&) = delete;
};

// Far enough on for any motion token to have ended.
double later() {
    return nowMs() + 10000.0;
}

} // namespace

TEST_CASE("polish: an item's hover fade ends, and is the end state at once under instant motion") {
    {
        Motion motion(false);
        ItemHover hover;
        const bool fading = hover.set(2);
        CHECK(fading == !reducedMotion());
        CHECK(hover.index() == 2);
        CHECK_FALSE(hover.tick(later())); // an animation ends: no frames when idle
        CHECK(hover.amount(2) == 1.0f);
        hover.set(5);
        CHECK_FALSE(hover.tick(later()));
        CHECK(hover.amount(5) == 1.0f);
        CHECK(hover.amount(2) == 0.0f); // the row left fades out to nothing
        hover.set(-1);
        CHECK_FALSE(hover.tick(later()));
        CHECK(hover.amount(5) == 0.0f);
        CHECK(hover.amount(-1) == 0.0f);
    }
    Motion instant(true);
    ItemHover hover;
    CHECK_FALSE(hover.set(1)); // nothing to tick: a still frame shows the plain hover fill
    CHECK(hover.amount(1) == 1.0f);
    CHECK_FALSE(hover.set(3));
    CHECK(hover.amount(3) == 1.0f);
    CHECK(hover.amount(1) == 0.0f);
}

TEST_CASE("polish: a selection indicator moves from the old item and stops on the new one") {
    const PointF origin{100, 50};
    const RectF from{110, 60, 20, 3};
    const RectF to{210, 60, 40, 3};
    {
        Motion motion(false);
        SelectionMove move;
        const bool started = move.start(0, from, origin);
        CHECK(started == !reducedMotion());
        if (started) {
            CHECK(move.from() == 0);
            const RectF now = move.at(to, origin); // at the start: still where it was
            CHECK(now.x == doctest::Approx(from.x));
            CHECK(now.width == doctest::Approx(from.width));
            // The widget scrolled 30 down while it moves: the indicator goes along.
            CHECK(move.at({to.x, to.y + 30, to.width, to.height}, {origin.x, origin.y + 30}).y == doctest::Approx(from.y + 30));
        }
        CHECK_FALSE(move.tick(later()));
        CHECK_FALSE(move.moving());
        CHECK(move.from() == -1);
        CHECK(move.at(to, origin).x == to.x);
    }
    Motion instant(true);
    SelectionMove move;
    CHECK_FALSE(move.start(0, from, origin));
    CHECK(move.at(to, origin).x == to.x); // exactly the target: a still frame as before
    CHECK(move.progress() == 1.0f);
}

TEST_CASE("polish: tab underlines and segments slide to a new choice and stop") {
    Motion motion(false);
    auto tabs = std::make_unique<TabBar>(std::vector<std::wstring>{L"Gizlilik", L"Yapay zekâ", L"Uygulamalar"}, 0);
    TabBar* bar = tabs.get();
    int changed = -1;
    bar->onChange = [&](int index) { changed = index; };
    Harness h(std::make_unique<OneChild>(std::move(tabs), RectF{0, 0, 400, 24}));
    CHECK(bar->onKeyDown({VK_RIGHT, false, false, false}));
    CHECK(changed == 1);
    CHECK(bar->selected() == 1);
    CHECK(bar->moving() == !reducedMotion());
    // What a page does inside onChange (Özellikler: counts in the titles; a sync from state): the
    // underline goes on sliding.
    bar->setTabs({L"Gizlilik 6", L"Yapay zekâ 2", L"Uygulamalar"});
    bar->setSelected(1);
    CHECK(bar->moving() == !reducedMotion());
    CHECK_FALSE(bar->tick(later()));
    CHECK_FALSE(bar->moving());
    bar->onKeyDown({VK_RIGHT, false, false, false});
    bar->setSelected(0); // set by code: no slide
    CHECK_FALSE(bar->moving());

    auto radio = std::make_unique<RadioGroup>(std::vector<std::wstring>{L"Koyu", L"Açık", L"Sistem"}, 0);
    RadioGroup* group = radio.get();
    Harness g(std::make_unique<OneChild>(std::move(radio), RectF{0, 0, 300, 24}));
    group->onKeyDown({VK_RIGHT, false, false, false});
    CHECK(group->selected() == 1);
    CHECK(group->moving() == !reducedMotion());
    group->setSelected(1); // Ayarlar › Tema: sync() inside onChange
    CHECK(group->moving() == !reducedMotion());
    CHECK_FALSE(group->tick(later()));
    CHECK_FALSE(group->moving());

    auto pills = std::make_unique<PillGroup>(std::vector<std::wstring>{L"Tümü", L"Seçili"}, 0);
    PillGroup* pill = pills.get();
    Harness p(std::make_unique<OneChild>(std::move(pills), RectF{0, 0, 300, 24}));
    pill->onKeyDown({VK_RIGHT, false, false, false});
    CHECK(pill->selected() == 1);
    CHECK_FALSE(pill->tick(later()));
    CHECK_FALSE(pill->moving());

    forceInstantMotion(true); // --render: the choice is simply there
    bar->onKeyDown({VK_RIGHT, false, false, false});
    CHECK(bar->selected() == 1);
    CHECK_FALSE(bar->moving());
}

TEST_CASE("polish: a ticked checkbox draws its tick in and stops; set by code it is just there") {
    Motion motion(false);
    auto field = std::make_unique<CheckField>(L"Kaydetmeden önce dur");
    CheckField* raw = field.get();
    Harness h(std::make_unique<OneChild>(std::move(field), RectF{0, 0, 300, 24}));
    h.down(10, 12);
    h.up(10, 12);
    CHECK(raw->checked());
    CHECK(raw->revealing() == !reducedMotion());
    raw->setChecked(true); // a page syncing from its state inside onChange: the tick goes on
    CHECK(raw->revealing() == !reducedMotion());
    CHECK_FALSE(raw->tick(later()));
    CHECK_FALSE(raw->revealing());
    h.down(10, 12);
    h.up(10, 12); // unticked: gone at once
    CHECK_FALSE(raw->checked());
    CHECK_FALSE(raw->revealing());
    raw->setChecked(true);
    CHECK_FALSE(raw->revealing());
}

TEST_CASE("polish: table rows — hover follows the pointer and fades; the focus ring lies on the row") {
    Motion motion(false);
    auto view = std::make_unique<TableView>(std::vector<TableColumn>{{L"Ad", 0}, {L"Boyut", 80}});
    TableView* table = view.get();
    const RectF rect{20, 10, 360, 200};
    Harness h(std::make_unique<OneChild>(std::move(view), rect));
    table->setRowCount(30);
    const float body = rect.y + TableView::kHeader;
    const float row = table->rowHeight();

    h.move(60, body + row * 2 + 4);
    CHECK(table->hoveredRow() == 2);
    CHECK(table->animating() == !reducedMotion());
    CHECK_FALSE(table->tick(later()));
    CHECK_FALSE(table->animating());
    h.move(60, body + row * 5 + 4);
    CHECK(table->hoveredRow() == 5);
    CHECK_FALSE(table->tick(later()));
    h.move(500, 500); // off the table: the highlight fades out, then nothing runs
    CHECK(table->hoveredRow() == -1);
    CHECK_FALSE(table->tick(later()));

    // The Host's ring: focusOffset + 1 outside the focus rectangle, 2 DIP wide, drawn inwards.
    const float ring = tokens::radius::focusOffset + 1;
    auto outer = [&](RectF r) { return RectF{r.x - ring, r.y - ring, r.width + 2 * ring, r.height + 2 * ring}; };
    table->setSelected(3);
    const RectF onRow = outer(table->focusRect());
    CHECK(onRow.x == doctest::Approx(rect.x)); // inside the table: the card around it cuts nothing
    CHECK(onRow.right() == doctest::Approx(rect.right()));
    CHECK(onRow.y == doctest::Approx(std::round(body + row * 3))); // on the row as painted
    CHECK(onRow.height == doctest::Approx(row));                     // not over the rows around it
    table->clearSelection();
    const RectF onBody = outer(table->focusRect()); // nothing selected: round the rows, inside the table
    CHECK(onBody.x == doctest::Approx(rect.x));
    CHECK(onBody.y == doctest::Approx(body));
    CHECK(onBody.bottom() == doctest::Approx(rect.bottom()));

    // A row's tick drawing in (the owner ticked it): runs and ends.
    table->revealCheck(4);
    CHECK(table->animating() == !reducedMotion());
    CHECK_FALSE(table->tick(later()));
    CHECK_FALSE(table->animating());
}

TEST_CASE("polish: a form row's hint ends inside the card's padding, clear of the scroll bar") {
    auto view = std::make_unique<FormView>(200.0f);
    FormView* form = view.get();
    const RectF rect{0, 0, 600, 400};
    Harness h(std::make_unique<OneChild>(std::move(view), rect));
    form->addSection(L"Motor ve performans");
    auto& engine = form->addRow<Toggle>(L"DISM motoru", std::wstring(400, L'x'), tokens::size::toggleW, L"");
    const RectF hint = form->hintRect(engine);
    CHECK(hint.x == doctest::Approx(engine.bounds().right() + 8));
    CHECK(hint.right() == doctest::Approx(rect.right() - 16)); // the label's inset, mirrored
    CHECK(hint.height == doctest::Approx(FormView::kRow));     // one line: rows keep their height
    CHECK(hint.y == doctest::Approx(engine.bounds().y - (FormView::kRow - FormView::kControl) / 2));

    for (int i = 0; i < 20; ++i) { // taller than the view: the scroll bar takes its lane
        form->addRow<Toggle>(std::format(L"Satır {}", i), std::wstring(), tokens::size::toggleW, L"");
    }
    CHECK(form->hintRect(engine).right() == doctest::Approx(rect.right() - ScrollBar::kWidth - 4 - 16));
}

TEST_CASE("audit UI-07: a widget destroyed by its own pointer-up is not clicked") {
    int clicks = 0;
    Harness h(std::make_unique<OneChild>(std::make_unique<GoneOnRelease>(clicks), RectF{0, 0, 100, 40}));
    h.down(10, 10);
    h.up(10, 10); // onClick() on the freed widget before the fix
    CHECK(clicks == 0);
    CHECK(h.host->root()->children().empty());
}

TEST_CASE("audit UI-07: a pressed widget that disables itself while dragged ends its press") {
    auto widget = std::make_unique<LockedOnDrag>();
    LockedOnDrag* raw = widget.get();
    Harness h(std::make_unique<OneChild>(std::move(widget), RectF{0, 0, 100, 40}));
    h.down(10, 10);
    h.move(20, 10); // went on to a null pressed widget before the fix
    CHECK(raw->moves == 1);
    CHECK_FALSE(raw->captured());
    h.up(20, 10);
}

TEST_CASE("audit PG-09: LogConsole::dropFront keeps the selection on the same lines") {
    auto console = std::make_unique<LogConsole>();
    LogConsole* raw = console.get();
    raw->copyFormat = [](const LogLine& line) { return line.message; };
    Harness h(std::make_unique<OneChild>(std::move(console), RectF{0, 0, 400, 116}));
    raw->setLines(numbered(10));

    raw->onKeyDown({'A', true, false, false}); // everything
    raw->dropFront(3);
    CHECK(raw->lineCount() == 7);
    CHECK(raw->selectedText() == L"3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n");

    raw->setLines(numbered(10));
    raw->onKeyDown({VK_HOME, false, false, false});
    for (int i = 0; i < 4; ++i) {
        raw->onKeyDown({VK_DOWN, false, false, false});
    }
    REQUIRE(raw->selectedText() == L"4\r\n");
    raw->dropFront(2);
    CHECK(raw->selectedText() == L"4\r\n"); // the line, not the row number
    raw->dropFront(5);                       // the selected line itself goes
    CHECK(raw->selectedText().empty());
    CHECK(raw->lineCount() == 3);

    raw->dropFront(100); // more than there is
    CHECK(raw->lineCount() == 0);
    raw->dropFront(1);
    CHECK(raw->lineCount() == 0);
}

TEST_CASE("audit PG-09: a LogConsole with a line limit keeps the newest lines") {
    auto console = std::make_unique<LogConsole>();
    LogConsole* raw = console.get();
    raw->copyFormat = [](const LogLine& line) { return line.message; };
    Harness h(std::make_unique<OneChild>(std::move(console), RectF{0, 0, 400, 116}));
    raw->setLineLimit(5);
    const auto lines = numbered(12);
    raw->append(lines);
    CHECK(raw->lineCount() == 5);
    raw->onKeyDown({'A', true, false, false});
    CHECK(raw->selectedText() == L"7\r\n8\r\n9\r\n10\r\n11\r\n");
    raw->setLineLimit(2);
    CHECK(raw->lineCount() == 2);
    raw->setLineLimit(0); // no limit again
    raw->append(lines);
    CHECK(raw->lineCount() == 14);
}

TEST_CASE("audit UI-06: typing and pasting stop at the text box's limit; clicks find the caret") {
    auto box = std::make_unique<SearchBox>(L"Ara");
    SearchBox* raw = box.get();
    raw->setPlain(true); // no clear button under the click at the right
    raw->setMaxLength(5);
    Harness h(std::make_unique<OneChild>(std::move(box), RectF{10, 10, 240, 24}));
    h.down(20, 20);
    h.up(20, 20);
    REQUIRE(h.host->focused() == raw);
    for (wchar_t c : std::wstring(L"abcdefgh")) {
        h.host->onChar(c);
    }
    CHECK(raw->text() == L"abcde");
    h.host->onKeyDown({VK_BACK, false, false, false});
    h.host->onChar(L'x');
    CHECK(raw->text() == L"abcdx");

    // Positions come from one kept layout now: far left is before the first character, far right
    // after the last.
    raw->setMaxLength(32);
    h.down(11, 20);
    h.up(11, 20);
    h.host->onChar(L'>');
    CHECK(raw->text() == L">abcdx");
    h.down(240, 20);
    h.up(240, 20);
    h.host->onChar(L'<');
    CHECK(raw->text() == L">abcdx<");
}

TEST_CASE("audit UI-06: without a limit set, a text box cuts nothing") {
    // A registry value's data in hex runs far past a path's 32 767 characters; it used to be cut,
    // and a box already holding more took not one more key.
    auto box = std::make_unique<SearchBox>(L"Ara");
    SearchBox* raw = box.get();
    raw->setPlain(true);
    Harness h(std::make_unique<OneChild>(std::move(box), RectF{10, 10, 240, 24}));
    h.down(20, 20);
    h.up(20, 20);
    REQUIRE(h.host->focused() == raw);
    raw->setText(std::wstring(33000, L'a')); // the caret goes to the end
    h.host->onChar(L'b');
    CHECK(raw->text().size() == 33001u);
    CHECK(raw->text().back() == L'b');
}

TEST_CASE("audit UI-09: a remembered text width is the measured one") {
    const TextStyles& text = *test::graphics().text;
    const float first = text.measure(L"Talep Üzerine Özellikler", tokens::TypeStyle::Body);
    CHECK(first > 0);
    CHECK(text.measure(L"Talep Üzerine Özellikler", tokens::TypeStyle::Body) == first);
    CHECK(text.measure(L"Talep Üzerine", tokens::TypeStyle::Body) < first); // by text, not by style alone
    const std::wstring longText(400, L'm');                                 // too long to be remembered
    CHECK(text.measure(longText, tokens::TypeStyle::Body) == text.measure(longText, tokens::TypeStyle::Body));
    CHECK(text.measure(longText, tokens::TypeStyle::Body) > first);
}

TEST_CASE("audit BASE-03: readFileBytes returns the bytes as they are") {
    const auto file = scratch(L"bytes.bin");
    std::string expected;
    for (int i = 0; i < 300000; ++i) {
        expected.push_back(static_cast<char>(i % 251)); // zeros, CR, LF, 0x1A among them
    }
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(expected.data(), static_cast<std::streamsize>(expected.size()));
    }
    const auto read = readFileBytes(file);
    REQUIRE(read.has_value());
    CHECK(*read == expected);

    const auto empty = scratch(L"empty.bin");
    { std::ofstream out(empty, std::ios::binary | std::ios::trunc); }
    const auto nothing = readFileBytes(empty);
    REQUIRE(nothing.has_value());
    CHECK(nothing->empty());

    CHECK_FALSE(readFileBytes(scratch(L"missing.bin")).has_value());
}

TEST_CASE("audit BASE-02: a new session log removes only the oldest session logs") {
    const auto dir = scratch(L"logs");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    auto touch = [&](const std::wstring& name) { std::ofstream out(dir / name, std::ios::binary); };
    for (int i = 0; i < 55; ++i) {
        touch(std::format(L"WinLove-20000101-0000{:02}.log", i));
    }
    // Not session logs: never touched, however old they look.
    touch(L"dism.log");
    touch(L"WinLove-notes.log");
    touch(L"WinLove-20000101-000000.log.bak");
    touch(L"WinLove-2000010A-000000.log");

    auto sink = log::makeFileSink(dir); // the 56th session log: 50 stay besides it at most
    sink.reset();

    int sessionLogs = 0;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring name = it->path().filename().wstring();
        if (name.starts_with(L"WinLove-2") && name.ends_with(L".log") && name.size() == 27 && name != L"WinLove-2000010A-000000.log") {
            ++sessionLogs;
        }
    }
    CHECK(sessionLogs == 51); // the 50 newest of the old ones + the new one
    CHECK_FALSE(std::filesystem::exists(dir / L"WinLove-20000101-000000.log"));
    CHECK_FALSE(std::filesystem::exists(dir / L"WinLove-20000101-000004.log"));
    CHECK(std::filesystem::exists(dir / L"WinLove-20000101-000005.log"));
    CHECK(std::filesystem::exists(dir / L"WinLove-20000101-000054.log"));
    CHECK(std::filesystem::exists(dir / L"dism.log"));
    CHECK(std::filesystem::exists(dir / L"WinLove-notes.log"));
    CHECK(std::filesystem::exists(dir / L"WinLove-20000101-000000.log.bak"));
    CHECK(std::filesystem::exists(dir / L"WinLove-2000010A-000000.log"));
    std::filesystem::remove_all(dir, ec);
}
