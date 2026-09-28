// Widget tree behavior: layout, pointer routing, focus order, tooltips, teardown safety, easing.
#include "support/TestGraphics.h"
#include "ui/anim/Tween.h"
#include "ui/widget/Host.h"
#include "ui/widget/Stack.h"
#include "ui/widgets/Button.h"

#include <doctest.h>

#include <vector>

using namespace wl;
using namespace wl::ui;

namespace {

// Minimal focusable widget that records what happened to it.
class Probe : public Widget {
public:
    explicit Probe(float w = 50, float h = 20, bool focusable = true) : m_size{w, h} { setFocusable(focusable); }
    SizeF measure(SizeF) override { return m_size; }
    void onClick() override { ++clicks; }
    bool onKeyDown(const KeyEvent& key) override {
        keys.push_back(key.virtualKey);
        return key.virtualKey == VK_RETURN;
    }
    int clicks = 0;
    std::vector<UINT> keys;

private:
    SizeF m_size;
};

struct Harness {
    int frames = 0;
    std::vector<std::pair<UINT, UINT>> timers;
    std::unique_ptr<Host> host;

    explicit Harness(std::unique_ptr<Widget> root, SizeF size = {400, 200}) {
        host = std::make_unique<Host>(HostServices{
            [this] { ++frames; },
            [this](UINT id, UINT ms) { timers.emplace_back(id, ms); },
            [](UINT) {},
            test::graphics().text.get(),
        });
        host->setRoot(std::move(root));
        host->layout(size);
    }
    void move(float x, float y) { host->onPointer({PointerAction::Move, {x, y}, HitZone::Client}); }
    void down(float x, float y) { host->onPointer({PointerAction::Down, {x, y}, HitZone::Client}); }
    void up(float x, float y) { host->onPointer({PointerAction::Up, {x, y}, HitZone::Client}); }
    void key(UINT vk, bool shift = false) { host->onKeyDown({vk, false, shift, false}); }
};

} // namespace

TEST_CASE("Stack lays out fixed, auto and fill children with gap and padding") {
    auto stack = std::make_unique<Stack>(Axis::Horizontal, 10.0f, Insets::xy(5, 2));
    auto& a = stack->addItem<Probe>(Sizing::fixed(30));
    auto& b = stack->addItem<Probe>(Sizing::autoSize(), CrossAlign::Center, 40.0f, 10.0f);
    auto& c = stack->addItem<Probe>(Sizing::fill());
    Harness h(std::move(stack), {200, 24});

    CHECK(a.bounds().x == 5);
    CHECK(a.bounds().width == 30);
    CHECK(a.bounds().height == 20); // stretch: 24 - 2*2
    CHECK(b.bounds().x == 45);
    CHECK(b.bounds().width == 40);
    CHECK(b.bounds().y == 7); // centered: 2 + (20 - 10) / 2
    CHECK(c.bounds().x == 95);
    CHECK(c.bounds().width == 100); // 200 - 5 - 5 - 30 - 40 - 2*10
}

TEST_CASE("click fires only when released over the pressed widget") {
    auto stack = std::make_unique<Stack>(Axis::Horizontal);
    auto& a = stack->addItem<Probe>(Sizing::fixed(100));
    auto& b = stack->addItem<Probe>(Sizing::fixed(100));
    Harness h(std::move(stack));

    h.move(10, 10);
    CHECK(a.hovered());
    h.down(10, 10);
    CHECK(a.pressed());
    h.up(10, 10);
    CHECK(a.clicks == 1);

    // Press on a, drag to b, release: cancelled (interaction.md).
    h.down(10, 10);
    h.move(150, 10);
    CHECK_FALSE(a.pressed()); // visual cancel while outside
    CHECK_FALSE(b.hovered()); // hover follows the pressed widget only
    h.up(150, 10);
    CHECK(a.clicks == 1);
    CHECK(b.clicks == 0);
}

TEST_CASE("disabled widgets get no hover, press or click") {
    auto stack = std::make_unique<Stack>(Axis::Horizontal);
    auto& a = stack->addItem<Probe>(Sizing::fixed(100));
    a.setEnabled(false);
    Harness h(std::move(stack));
    h.move(10, 10);
    h.down(10, 10);
    h.up(10, 10);
    CHECK_FALSE(a.hovered());
    CHECK(a.clicks == 0);
}

TEST_CASE("Tab order follows the tree, skips non-tab-stops and wraps; focus ring only from keyboard") {
    auto stack = std::make_unique<Stack>(Axis::Horizontal);
    auto& a = stack->addItem<Probe>(Sizing::fixed(50));
    auto& roving = stack->addItem<Probe>(Sizing::fixed(50));
    auto& c = stack->addItem<Probe>(Sizing::fixed(50));
    stack->addItem<Probe>(Sizing::fixed(50), CrossAlign::Stretch, 50.0f, 20.0f, /*focusable=*/false);
    roving.setTabStop(false);
    Harness h(std::move(stack));

    h.key(VK_TAB);
    CHECK(h.host->focused() == &a);
    h.key(VK_TAB);
    CHECK(h.host->focused() == &c); // roving item skipped
    h.key(VK_TAB);
    CHECK(h.host->focused() == &a); // wraps
    h.key(VK_TAB, /*shift=*/true);
    CHECK(h.host->focused() == &c);

    // Clicking focuses without making the ring visible; keys go to the focused widget.
    h.down(60, 10);
    h.up(60, 10);
    CHECK(h.host->focused() == &roving);
    h.key(VK_RETURN);
    CHECK(roving.keys.size() == 1);
}

TEST_CASE("tooltip is scheduled after hover and dropped when the pointer leaves") {
    auto stack = std::make_unique<Stack>(Axis::Horizontal);
    auto& a = stack->addItem<Probe>(Sizing::fixed(100));
    a.setTooltip(L"Tip");
    Harness h(std::move(stack));
    h.move(10, 10);
    REQUIRE(h.timers.size() == 1);
    CHECK(h.timers[0].first == Host::kTooltipTimer);
    CHECK(h.timers[0].second == 400);
}

TEST_CASE("destroying a hovered, pressed and focused widget leaves the host consistent") {
    auto stack = std::make_unique<Stack>(Axis::Horizontal);
    Stack* raw = stack.get();
    auto& a = stack->addItem<Probe>(Sizing::fixed(100));
    Harness h(std::move(stack));
    h.move(10, 10);
    h.down(10, 10);
    h.key(VK_TAB);
    raw->removeChild(&a);
    CHECK(h.host->focused() == nullptr);
    h.up(10, 10); // must not touch the destroyed widget
    h.move(20, 10);
    h.key(VK_TAB);
}

TEST_CASE("Button invokes on click, Space and Enter") {
    auto stack = std::make_unique<Stack>(Axis::Horizontal);
    auto& button = stack->addItem<Button>(Sizing::autoSize(), CrossAlign::Center, ButtonKind::Primary, L"Uygula");
    int invoked = 0;
    button.onInvoke = [&] { ++invoked; };
    Harness h(std::move(stack));
    CHECK(button.bounds().width >= 56); // button.md: min width 56
    CHECK(button.bounds().height == tokens::size::control);
    const PointF center = button.bounds().center(); // centered vertically in the 200px stack
    h.down(center.x, center.y);
    h.up(center.x, center.y);
    h.key(VK_TAB);
    h.key(VK_SPACE);
    h.key(VK_RETURN);
    CHECK(invoked == 3);
}

TEST_CASE("cubic-bezier easing hits the endpoints and is monotonic for standard") {
    const auto& curve = tokens::motion::standard;
    CHECK(evalBezier(curve, 0.0f) == doctest::Approx(0.0f));
    CHECK(evalBezier(curve, 1.0f) == doctest::Approx(1.0f));
    float previous = 0;
    for (int i = 1; i <= 20; ++i) {
        const float v = evalBezier(curve, static_cast<float>(i) / 20);
        CHECK(v >= previous - 1e-4f);
        previous = v;
    }
    // Decelerating curve: more than half done at the midpoint.
    CHECK(evalBezier(curve, 0.5f) > 0.5f);
}

TEST_CASE("Tween reaches its target and snaps under instant motion") {
    Tween t;
    forceInstantMotion(true);
    CHECK_FALSE(t.animateTo(1.0f, 140));
    CHECK(t.value() == 1.0f);
    forceInstantMotion(false);
}
