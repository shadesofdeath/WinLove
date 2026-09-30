// P04: toggle → ChangeSet mapping, statuses, reset, and the queue's lifetime (one mounted image).
#include "app/controllers/FeatureController.h"

#include <doctest.h>

#include <filesystem>

using namespace wl;
using namespace wl::app;
using core::OptionalFeature;
using core::ops::OpKind;
using S = core::ServicingState;
using Status = FeatureController::Status;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"features";
    std::filesystem::create_directories(dir);
    return dir / name;
}

OptionalFeature feature(std::wstring name, S state) {
    return {OptionalFeature::Kind::Feature, name, name, {}, state, 0, false};
}

OptionalFeature capability(std::wstring name, S state, std::uint64_t size) {
    return {OptionalFeature::Kind::Capability, name, name, {}, state, size, false};
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    FeatureController controller{state, [](std::function<void()> fn) { fn(); }};

    Fixture() {
        state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    }
};

} // namespace

TEST_CASE("operationFor: enabled feature → disable, disabled → enable, capability → remove with size") {
    CHECK(FeatureController::operationFor(feature(L"NetFx3", S::Installed)).kind == OpKind::DisableFeature);
    CHECK(FeatureController::operationFor(feature(L"WSL", S::Staged)).kind == OpKind::EnableFeature);
    CHECK(FeatureController::operationFor(feature(L"X", S::Removed)).kind == OpKind::EnableFeature);
    const auto op = FeatureController::operationFor(capability(L"OpenSSH.Client", S::Installed, 6'000'000));
    CHECK(op.kind == OpKind::RemoveCapability);
    CHECK(op.sizeDelta == -6'000'000);
}

TEST_CASE("toggle queues, toggling again restores; status and target follow the queue") {
    Fixture f;
    const auto wsl = feature(L"WSL", S::Staged);
    CHECK(f.controller.status(wsl) == Status::Disabled);
    CHECK_FALSE(f.controller.targetOn(wsl));
    f.controller.toggle(wsl);
    CHECK(f.state.changes().size() == 1);
    CHECK(f.controller.status(wsl) == Status::WillEnable);
    CHECK(f.controller.targetOn(wsl));
    f.controller.toggle(wsl);
    CHECK(f.state.changes().empty());
    CHECK(f.controller.status(wsl) == Status::Disabled);

    const auto ssh = capability(L"OpenSSH.Client", S::Installed, 6);
    CHECK(f.controller.status(ssh) == Status::Installed);
    f.controller.toggle(ssh);
    CHECK(f.controller.status(ssh) == Status::WillRemove);
    CHECK_FALSE(f.controller.targetOn(ssh));
}

TEST_CASE("absent capabilities cannot be toggled (FoD needs source media)") {
    Fixture f;
    const auto absent = capability(L"Language.Speech", S::NotPresent, 0);
    CHECK_FALSE(f.controller.canToggle(absent));
    f.controller.toggle(absent);
    CHECK(f.state.changes().empty());
}

TEST_CASE("resetChanges drops only feature operations; a new mount clears the queue") {
    Fixture f;
    f.controller.toggle(feature(L"A", S::Staged));
    f.controller.toggle(feature(L"B", S::Installed));
    f.state.queue(core::ops::Operation{OpKind::SetServiceStart, L"DiagTrack", L"4"});
    CHECK(f.controller.queuedCount() == 2);
    f.controller.resetChanges();
    CHECK(f.controller.queuedCount() == 0);
    CHECK(f.state.changes().size() == 1); // the service change stays

    f.state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 2, L"Home"});
    CHECK(f.state.changes().empty());
    CHECK_FALSE(f.state.optionalFeatures().has_value());
}

TEST_CASE("a permanent capability cannot be queued, and says so") {
    Fixture f;
    auto sense = capability(L"Microsoft.Windows.Sense.Client~~~~", S::Installed, 0);
    sense.permanent = true;
    CHECK_FALSE(f.controller.canToggle(sense));
    CHECK(f.controller.status(sense) == Status::Permanent);
    f.controller.toggle(sense);
    CHECK(f.state.changes().empty());
    // Only what is installed is "permanent"; the flag on an absent one changes nothing.
    auto absent = capability(L"X~~~~", S::NotPresent, 0);
    absent.permanent = true;
    CHECK(f.controller.status(absent) == Status::Disabled);
}

TEST_CASE("permanent capabilities are read from the package manifests") {
    // Windows 11 25H2: the Defender for Endpoint client, and an ordinary FoD next to it.
    const std::string sense = R"(<assembly><assemblyIdentity name="Microsoft-Windows-SenseClient-FoD-Package" version="10.0.26100.8036" />
      <package identifier="KB777778" permanence="permanent" releaseType="OnDemand Pack">
        <declareCapability><capability><capabilityIdentity name="Microsoft.Windows.Sense.Client" /></capability></declareCapability>
      </package></assembly>)";
    const std::string steps = R"(<assembly><package identifier="KB777778" releaseType="OnDemand Pack">
        <declareCapability><capability><capabilityIdentity name="App.StepsRecorder" version="1.0" /></capability></declareCapability>
      </package></assembly>)";
    CHECK(core::permanentCapabilitiesIn(sense) == std::vector<std::wstring>{L"Microsoft.Windows.Sense.Client"});
    CHECK(core::permanentCapabilitiesIn(steps).empty());
    CHECK(core::permanentCapabilitiesIn("").empty());
    CHECK(core::permanentCapabilitiesIn(R"(<package permanence="permanent"><capabilityIdentity version="1" /></package>)").empty());
    CHECK(core::permanentCapabilitiesIn(R"(<package permanence="permanent"><capabilityIdentity name="A"/><x/><capabilityIdentity name="B" /></package>)") ==
          std::vector<std::wstring>{L"A", L"B"});
}
