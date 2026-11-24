// DPI: the double-click area comes from the window's DPI (Window::doubleClickArea through
// HostServices), not from the system DPI's pixels taken as DIPs.
#include "support/TestGraphics.h"
#include "ui/widget/Host.h"

#include <doctest.h>

#include <memory>

using namespace wl;

namespace {

class DoubleProbe : public ui::Widget {
public:
    void onDoubleClick() override { ++doubles; }
    int doubles = 0;
};

} // namespace

TEST_CASE("DPI: the double-click area comes from the host's service, in DIPs") {
    auto root = std::make_unique<DoubleProbe>();
    DoubleProbe* probe = root.get();
    ui::SizeF area{4, 4}; // ±2 DIP
    ui::HostServices services{[] {}, [](UINT, UINT) {}, [](UINT) {}, test::graphics().text.get()};
    services.doubleClickArea = [&area] { return area; };
    ui::Host host(std::move(services));
    host.setRoot(std::move(root));
    host.layout({200, 100});
    auto click = [&](float x, float y) {
        host.onPointer({ui::PointerAction::Move, {x, y}, ui::HitZone::Client});
        host.onPointer({ui::PointerAction::Down, {x, y}, ui::HitZone::Client});
        host.onPointer({ui::PointerAction::Up, {x, y}, ui::HitZone::Client});
    };
    click(50, 50);
    click(51.5f, 50); // within ±2
    CHECK(probe->doubles == 1);
    click(100, 50);
    click(103, 50); // 3 DIP away: two clicks
    CHECK(probe->doubles == 1);
}
