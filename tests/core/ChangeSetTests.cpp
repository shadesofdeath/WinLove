// Change queue rules (D-003) and planning order.
#include "core/ops/ChangeSet.h"
#include "core/ops/Planner.h"

#include <doctest.h>

using namespace wl;
using namespace wl::core::ops;

namespace {
Operation disable(std::wstring name) { return {OpKind::DisableFeature, std::move(name)}; }
Operation enable(std::wstring name) { return {OpKind::EnableFeature, std::move(name)}; }
} // namespace

TEST_CASE("ChangeSet: one operation per slot, inverse cancels, case-insensitive targets") {
    ChangeSet set;
    set.add(disable(L"MediaPlayback"));
    set.add(disable(L"mediaplayback")); // same slot: replaced, not duplicated
    CHECK(set.size() == 1);
    set.add(enable(L"MEDIAPLAYBACK"));   // inverse: back to the original state
    CHECK(set.empty());
    set.add({OpKind::RemovePackage, L"Pkg~1", L"", Risk::High, -1000});
    set.add({OpKind::RemovePackage, L"Pkg~2", L"", Risk::Low, -500});
    CHECK(set.count(OpKind::RemovePackage) == 2);
    CHECK(set.estimatedSizeDelta() == -1500);
    CHECK(set.find(OpKind::RemovePackage, L"pkg~1") != nullptr);
    CHECK(set.remove(OpKind::RemovePackage, L"PKG~1"));
    CHECK_FALSE(set.remove(OpKind::RemovePackage, L"PKG~1"));
    CHECK(set.size() == 1);
}

TEST_CASE("ChangeSet: undo / redo walk the history and new edits drop the redo branch") {
    ChangeSet set;
    const auto v0 = set.version();
    set.add(disable(L"A"));
    set.add(disable(L"B"));
    CHECK(set.version() > v0);
    CHECK(set.undo());
    CHECK(set.size() == 1);
    CHECK(set.undo());
    CHECK(set.empty());
    CHECK_FALSE(set.undo());
    CHECK(set.redo());
    CHECK(set.size() == 1);
    set.add(disable(L"C")); // new edit: redo branch gone
    CHECK_FALSE(set.redo());
    CHECK(set.size() == 2);
}

TEST_CASE("ChangeSet: JSON round trip (preset format) and validation") {
    ChangeSet set;
    set.add({OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\X\\Y", L"dword:1", Risk::Medium, 0});
    set.add({OpKind::RemoveCapability, L"App.StepsRecorder~~~~0.0.1.0", L"", Risk::Low, -2048});
    auto loaded = ChangeSet::fromJson(set.toJson());
    REQUIRE_MESSAGE(loaded.has_value(), describe(loaded.error()).c_str());
    REQUIRE(loaded->size() == 2);
    CHECK(loaded->operations()[0].value == L"dword:1");
    CHECK(loaded->operations()[0].risk == Risk::Medium);
    CHECK(loaded->operations()[1].sizeDelta == -2048);
    CHECK_FALSE(loaded->canUndo()); // loading is not an edit

    CHECK(ChangeSet::fromJson("{}").error().code == ErrorCode::ParseError);
    CHECK(ChangeSet::fromJson(R"({"format":"winlove.changeset","version":9})").error().code == ErrorCode::Unsupported);
    CHECK(ChangeSet::fromJson(R"({"format":"winlove.changeset","version":1,"operations":[{"kind":"nope","target":"x"}]})")
              .error().code == ErrorCode::ParseError);
}

TEST_CASE("Planner: removals, features, drivers, updates, settings — user order kept inside a phase") {
    ChangeSet set;
    set.add({OpKind::SetServiceStart, L"DiagTrack", L"disabled"});
    set.add(disable(L"F1"));
    set.add({OpKind::RemovePackage, L"P1", L"", Risk::High});
    set.add({OpKind::AddDriver, L"C:\\drivers\\x.inf"});
    set.add(disable(L"F2"));
    set.add({OpKind::RemoveCapability, L"C1"});
    const ApplyPlan p = plan(set);
    REQUIRE(p.steps.size() == 6);
    CHECK(p.steps[0].operation.target == L"P1");
    CHECK(p.steps[1].operation.target == L"C1");
    CHECK(p.steps[2].operation.target == L"F1");
    CHECK(p.steps[3].operation.target == L"F2");
    CHECK(p.steps[4].phase == Phase::Drivers);
    CHECK(p.steps[5].phase == Phase::Settings);
    CHECK(p.hasHighRisk());
    CHECK(p.warnings.size() == 1);
}
