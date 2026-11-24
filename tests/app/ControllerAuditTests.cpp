// docs/AUDIT-2026-11-10.md, controllers: the behaviour its fixes changed, pinned.
#include "app/catalog/AppxCatalog.h"
#include "app/controllers/ComponentController.h"
#include "app/controllers/PostSetupController.h"
#include "app/controllers/StartPinsController.h"
#include "app/controllers/UnattendController.h"
#include "core/image/StartMenu.h"
#include "core/unattend/Welcome.h"
#include "support/UiThread.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"controller-audit";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

core::AppxComponent makeApp(const wchar_t* identity, std::uint64_t size) {
    core::AppxComponent c;
    c.package.displayName = identity;
    c.package.packageName = std::wstring(identity) + L"_1.0.0.0_neutral_~_8wekyb3d8bbwe";
    c.size = size;
    return c;
}

std::size_t itemCount(const std::vector<ComponentController::Group>& groups) {
    std::size_t n = 0;
    for (const auto& group : groups) {
        n += group.items.size();
    }
    return n;
}

// Counts the queue's notifications.
struct QueueTold {
    explicit QueueTold(AppState& state) {
        state.subscribe([this](AppState::Change change) { count += change == AppState::Change::Queue ? 1 : 0; });
    }
    int count = 0;
};

} // namespace

TEST_CASE("audit CTL-04: the component list is kept over queue changes and built again when the image's lists change") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    auto catalog = AppxCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/appx.json"));
    REQUIRE(catalog);
    // Results of the background reads are dropped: this test is about what the state holds.
    ComponentController controller(state, std::move(*catalog), Language::Turkish, [](std::function<void()>) {});
    state.setMounted(MountedImage{L"C:\\wl-no-such-mount", L"C:\\w\\install.wim", 1, L"Pro"});
    CHECK(controller.groups().empty()); // no list yet

    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\wl-no-such-mount",
                                         {makeApp(L"Microsoft.GamingApp", 400), makeApp(L"Contoso.Unknown", 3)}, {}});
    auto groups = controller.groups();
    REQUIRE(itemCount(groups) == 2);

    // A queue change: the same items, and what is queued shows (the queue is asked, not the list).
    const auto item = groups.front().items.front();
    controller.toggle(item);
    CHECK(controller.queued(item));
    CHECK(itemCount(controller.groups()) == 2);
    CHECK(controller.dependents(item).empty());
    controller.toggle(item);
    CHECK_FALSE(controller.queued(item));

    // Several queue changes told as one (a batch) must not leave an old list behind either.
    {
        const auto batch = state.batchQueue();
        state.queue(Operation{OpKind::DisableFeature, L"a"});
        state.queue(Operation{OpKind::DisableFeature, L"b"});
    }
    CHECK(itemCount(controller.groups()) == 2);

    // The image's list read again: the new one.
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\wl-no-such-mount",
                                         {makeApp(L"Microsoft.GamingApp", 400), makeApp(L"Contoso.Unknown", 3),
                                          makeApp(L"Contoso.Other", 5)},
                                         {}});
    CHECK(itemCount(controller.groups()) == 3);
    CHECK(controller.modeItems().empty()); // no system catalog: no cleanup, no shrink

    // A list dropped (Uygula left the image mounted) or the image gone: nothing from before.
    state.setAppxList(std::nullopt);
    CHECK(controller.groups().empty());
    state.setMounted(std::nullopt);
    CHECK(controller.groups().empty());
}

TEST_CASE("audit CTL-06: the queued welcome follows an answer with one queue change; the same plan changes nothing") {
    AppState state{scratch(L"welcome-recent.json"), scratch(L"welcome-settings.json")};
    UnattendController controller(state);
    QueueTold told(state);

    controller.setWelcome(true);
    REQUIRE(controller.welcomeQueued());
    CHECK(told.count == 1);

    controller.edit([](core::UnattendOptions& o) {
        o.randomComputerName = false;
        o.computerName = L"OFIS";
    });
    CHECK(controller.welcomePlan().computerName == L"OFIS");
    CHECK(told.count == 2);

    // One key more: one notification, one undo step back to the name before it.
    controller.edit([](core::UnattendOptions& o) { o.computerName = L"OFIS-PC"; });
    CHECK(controller.welcomePlan().computerName == L"OFIS-PC");
    CHECK(told.count == 3);
    const std::size_t queued = state.changes().size();

    // The plan as it is queued: no edit, no notification.
    const std::uint64_t version = state.changes().version();
    controller.setWelcomePlan(controller.welcomePlan());
    CHECK(told.count == 3);
    CHECK(state.changes().version() == version);

    CHECK(state.undoQueue());
    CHECK(state.changes().size() == queued); // both files still queued: the step was the whole edit
    CHECK(controller.welcomePlan().computerName == L"OFIS");
}

TEST_CASE("audit CTL-06: a new list of Start pins is one queue change and one undo step") {
    AppState state{scratch(L"pins-recent.json"), scratch(L"pins-settings.json")};
    test::UiThread ui;
    StartPinsController controller{state, ui.post()};
    QueueTold told(state);
    const core::StartApp terminal{core::StartApp::Kind::Packaged, L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", L"Terminal", {}};

    const core::StartApp edge{core::StartApp::Kind::Packaged, L"Microsoft.MicrosoftEdge.Stable_8wekyb3d8bbwe!App", L"Edge", {}};

    controller.setPins({edge});
    CHECK(told.count == 1);
    REQUIRE(controller.pins().size() == 1);

    controller.setPins({edge, terminal});
    CHECK(told.count == 2); // was 3: taken out, then put in
    CHECK(controller.pins().size() == 2);

    CHECK(state.undoQueue());
    CHECK(controller.pins().size() == 1); // not the queue without any plan in between
    CHECK(controller.mode() != StartPinsController::Mode::Windows);
}

TEST_CASE("audit CTL-13: the Start app list is dropped once as an Apply run ends, not at each later notification") {
    AppState state{scratch(L"pins-run-recent.json"), scratch(L"pins-run-settings.json")};
    StartPinsController controller{state, [](std::function<void()>) {}}; // background reads are dropped
    state.setMounted(MountedImage{L"C:\\wl-no-such-mount", L"C:\\w\\install.wim", 1, L"Pro"});
    controller.preload();
    REQUIRE(controller.apps() != nullptr);

    state.setApplyRun(AppState::ApplyRun{}); // running: the list stays
    CHECK(controller.apps() != nullptr);
    state.applyRunMutable()->stage = AppState::ApplyRun::Stage::Done;
    state.notifyApply(); // the run ends: the image's apps may have changed
    CHECK(controller.apps() == nullptr); // being read again

    controller.preload();
    REQUIRE(controller.apps() != nullptr);
    state.notifyApply(); // the finished run told again (saved, discarded): the list read since stays
    CHECK(controller.apps() != nullptr);
}

TEST_CASE("audit CTL-18: the post-setup plan of one image is not handed out for the next") {
    AppState state{scratch(L"post-recent.json"), scratch(L"post-settings.json")};
    PostSetupController controller{state};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    controller.add(core::PostSetupStep{core::PostSetupStep::Type::Command, L"power", L"powercfg /h off", {}, true});
    REQUIRE(controller.stepCount() == 1); // kept under this queue's version
    const std::uint64_t kept = state.changes().version();

    // Another image: a new queue that counts from 0. Bring it to the version the plan was kept
    // under without asking for the plan in between.
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 2, L"Home"});
    for (int i = 0; i < 16 && state.changes().version() < kept; ++i) {
        state.queue(Operation{OpKind::DisableFeature, L"feature" + std::to_wstring(i)});
    }
    CHECK(controller.stepCount() == 0);
    CHECK(controller.plan().programs.empty());
}
