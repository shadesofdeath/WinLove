// P14: the post-setup plan as one queue operation — edits, ordering, options, presets.
#include "app/controllers/PostSetupController.h"
#include "base/Utf8.h"

#include <doctest.h>

#include <filesystem>

using namespace wl;
using namespace wl::app;
using core::PostSetupPlan;
using core::ops::OpKind;
using Step = core::PostSetupStep;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"postsetup-app";
    std::filesystem::create_directories(dir);
    return dir / name;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    PostSetupController controller{state};
    Fixture() { state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"}); }

    [[nodiscard]] std::vector<std::wstring> names() const {
        std::vector<std::wstring> result;
        for (const auto& step : controller.plan().steps) {
            result.push_back(step.name);
        }
        return result;
    }
};

Step command(std::wstring name, std::wstring line) {
    return Step{Step::Type::Command, std::move(name), std::move(line), {}, true};
}

} // namespace

TEST_CASE("post-setup controller: the whole plan is one queue operation") {
    Fixture f;
    CHECK(f.controller.stepCount() == 0);
    f.controller.add(Step{Step::Type::Winget, L"7-Zip", L"7zip.7zip", {}, true});
    f.controller.add(command(L"power", L"powercfg /h off"));
    f.controller.add(command(L"restart", L"shutdown /r /t 30"));

    REQUIRE(f.state.changes().size() == 1);
    const auto& op = f.state.changes().operations().front();
    CHECK(op.kind == OpKind::SetPostSetup);
    CHECK(op.target == PostSetupController::kTarget);
    CHECK(op.risk == core::ops::Risk::Medium);
    const auto stored = core::postSetupFromJson(utf8::fromWide(op.value));
    REQUIRE(stored);
    CHECK(*stored == f.controller.plan());
    CHECK(f.names() == std::vector<std::wstring>{L"7-Zip", L"power", L"restart"});

    // Order: up / down, stopping at the ends.
    CHECK(f.controller.move(2, -1));
    CHECK(f.names() == std::vector<std::wstring>{L"7-Zip", L"restart", L"power"});
    CHECK_FALSE(f.controller.move(0, -1));
    CHECK_FALSE(f.controller.move(2, +1));
    CHECK(f.controller.move(0, +1));
    CHECK(f.names() == std::vector<std::wstring>{L"restart", L"7-Zip", L"power"});

    // Wait applies to commands only.
    f.controller.toggleWait(0);
    CHECK_FALSE(f.controller.plan().steps[0].wait);
    f.controller.toggleWait(1); // winget
    CHECK(f.controller.plan().steps[1].wait);

    f.controller.replace(2, command(L"power plan", L"powercfg /setactive x"));
    CHECK(f.controller.plan().steps[2].source == L"powercfg /setactive x");

    f.controller.remove(1);
    CHECK(f.names() == std::vector<std::wstring>{L"restart", L"power plan"});
    CHECK(f.state.changes().size() == 1);
    f.controller.remove(0);
    f.controller.remove(0);
    CHECK(f.state.changes().empty()); // no steps: nothing to apply
}

TEST_CASE("post-setup controller: options chosen before the first step go into the plan") {
    Fixture f;
    f.controller.setWhen(PostSetupPlan::When::SetupComplete);
    f.controller.setContinueOnError(false);
    CHECK(f.state.changes().empty());
    CHECK(f.controller.plan().when == PostSetupPlan::When::SetupComplete);
    CHECK_FALSE(f.controller.plan().continueOnError);

    f.controller.add(command(L"x", L"echo x"));
    CHECK(f.controller.plan().when == PostSetupPlan::When::SetupComplete);
    CHECK_FALSE(f.controller.plan().continueOnError);
    f.controller.setWhen(PostSetupPlan::When::FirstLogon);
    CHECK(f.controller.plan().when == PostSetupPlan::When::FirstLogon);
    CHECK(f.state.changes().size() == 1);
}

TEST_CASE("post-setup controller: a preset carries the plan; a broken one reads as empty") {
    Fixture f;
    f.controller.add(command(L"G\u00fc\u00e7", L"powercfg /h off"));
    f.controller.add(Step{Step::Type::Copy, L"files", L"C:\\does\\not\\exist", L"%PUBLIC%\\Desktop", true});
    const PostSetupPlan plan = f.controller.plan();

    const auto preset = core::ops::ChangeSet::fromJson(f.state.changes().toJson());
    REQUIRE(preset);
    Fixture g;
    for (const auto& op : preset->operations()) {
        g.state.queue(op);
    }
    CHECK(g.controller.plan() == plan);

    g.state.queue(core::ops::Operation{OpKind::SetPostSetup, PostSetupController::kTarget, L"{ not json"});
    CHECK(g.controller.plan().steps.empty());
    g.controller.add(command(L"again", L"echo again")); // usable again: the broken value is replaced
    CHECK(g.controller.stepCount() == 1);
}
