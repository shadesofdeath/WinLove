// P05: plan groups + estimates, duration formatting, the Apply page mode, and what start()
// refuses (no mount / empty queue / not elevated is covered by canStart()).
#include "app/Format.h"
#include "app/controllers/ApplyController.h"
#include "app/pages/ApplyPage.h"

#include <doctest.h>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Phase;

namespace {
std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"apply";
    std::filesystem::create_directories(dir);
    return dir / name;
}
} // namespace

TEST_CASE("plan groups follow the phase order and sum their estimates") {
    core::ops::ChangeSet set;
    set.add(Operation{OpKind::EnableFeature, L"WSL"});
    set.add(Operation{OpKind::RemoveCapability, L"OpenSSH.Client"});
    set.add(Operation{OpKind::DisableFeature, L"NetFx3"});
    set.add(Operation{OpKind::SetServiceStart, L"DiagTrack", L"4"});
    const auto plan = core::ops::plan(set);
    const auto groups = core::ops::groups(plan);
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].phase == Phase::Remove);
    CHECK(groups[1].phase == Phase::Features);
    CHECK(groups[1].count == 2);
    CHECK(groups[1].first == 1);
    CHECK(groups[1].estimateSeconds ==
          doctest::Approx(core::ops::estimateSeconds(OpKind::EnableFeature) + core::ops::estimateSeconds(OpKind::DisableFeature)));
    CHECK(groups[2].phase == Phase::Settings);
}

TEST_CASE("formatDuration: exact and approximate, Turkish and English") {
    CHECK(formatDuration(168, Language::Turkish) == L"2 dk 48 sn");
    CHECK(formatDuration(38, Language::Turkish) == L"38 sn");
    CHECK(formatDuration(3840, Language::English) == L"1 h 4 min");
    CHECK(formatDuration(95, Language::Turkish, true) == L"~2 dk");
    CHECK(formatDuration(0.2, Language::English, true) == L"~1 s");
}

TEST_CASE("Apply page mode: no mount, empty queue, summary, running, done") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::NoMount);
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Empty);
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Summary);

    AppState::ApplyRun run;
    state.setApplyRun(run);
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Running);
    run.stage = AppState::ApplyRun::Stage::Done;
    state.setApplyRun(run);
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Summary); // queue still has work
    state.setMounted(std::nullopt);                                // committed: unmounted, queue cleared
    CHECK(ApplyPage::modeFor(state) == ApplyPage::Mode::Done);
}

TEST_CASE("ApplyController: high-risk list and canStart") {
    AppState state{scratch(L"recent2.json"), scratch(L"settings2.json")};
    ApplyController controller(state, {[](std::function<void()> fn) { fn(); }, {}, {}, {}});
    CHECK_FALSE(controller.canStart()); // nothing mounted
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    CHECK_FALSE(controller.canStart()); // empty queue
    state.queue(Operation{OpKind::RemoveCapability, L"OpenSSH.Client", L"", core::ops::Risk::High});
    state.queue(Operation{OpKind::EnableFeature, L"WSL"});
    CHECK(controller.canStart());
    REQUIRE(controller.highRisk().size() == 1);
    CHECK(controller.highRisk()[0].target == L"OpenSSH.Client");
}
