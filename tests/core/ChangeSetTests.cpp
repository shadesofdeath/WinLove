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
    // Wrongly typed fields are parse errors, not exceptions (a preset is user input).
    CHECK(ChangeSet::fromJson(R"({"format":"winlove.changeset","version":"1"})").error().code == ErrorCode::ParseError);
    CHECK(ChangeSet::fromJson(R"({"format":"winlove.changeset","version":1,"operations":[{"kind":"addDriver","target":5}]})")
              .error().code == ErrorCode::ParseError);
    CHECK(ChangeSet::fromJson(R"({"format":"winlove.changeset","version":1,"operations":[7]})").error().code ==
          ErrorCode::ParseError);
}

TEST_CASE("Planner: features, drivers, updates, removals, settings — user order kept inside a phase") {
    ChangeSet set;
    set.add({OpKind::SetServiceStart, L"DiagTrack", L"disabled"});
    set.add(disable(L"F1"));
    set.add({OpKind::RemovePackage, L"P1", L"", Risk::High});
    set.add({OpKind::AddDriver, L"C:\\drivers\\x.inf"});
    set.add(disable(L"F2"));
    set.add({OpKind::RemoveCapability, L"C1"});
    set.add({OpKind::AddPackage, L"C:\\u\\kb.msu", L"lcu"});
    const ApplyPlan p = plan(set);
    REQUIRE(p.steps.size() == 7);
    CHECK(p.steps[0].operation.target == L"F1");
    CHECK(p.steps[1].operation.target == L"F2");
    CHECK(p.steps[2].phase == Phase::Drivers);
    // D-124: the update goes into the whole image, the removals after it (the reference tool's order; the other
    // way round a cumulative update failed with 0x80073712 on the removed packages' components).
    CHECK(p.steps[3].phase == Phase::Updates);
    CHECK(p.steps[4].operation.target == L"P1");
    CHECK(p.steps[5].operation.target == L"C1");
    CHECK(p.steps[6].phase == Phase::Settings);
    CHECK(p.hasHighRisk());
    CHECK(p.warnings.size() == 1);
}

TEST_CASE("ChangeSet batches: one undo step for addAll / removeAll") {
    ChangeSet set;
    set.addAll({{OpKind::SetRegistryValue, L"HKCU\\A::x", L"dword:00000001"},
                {OpKind::SetRegistryValue, L"HKCU\\A::y", L"dword:00000002"}});
    CHECK(set.size() == 2);
    CHECK(set.undo());
    CHECK(set.empty()); // the whole batch was one step
    CHECK(set.redo());
    CHECK(set.removeAll({{OpKind::SetRegistryValue, L"HKCU\\A::x"}, {OpKind::SetRegistryValue, L"HKCU\\A::nope"}}) == 1);
    CHECK(set.size() == 1);
    CHECK(set.removeAll({{OpKind::SetRegistryValue, L"HKCU\\A::nope"}}) == 0);
}

TEST_CASE("audit: the slot index keeps find()'s rules — first match, any case, any script") {
    ChangeSet set;
    set.add({OpKind::RemovePackage, L"Çalışma~1"});
    set.add({OpKind::RemovePackage, L"Résumé"});
    set.add({OpKind::RemoveAppx, L"Résumé"}); // same target, another kind: another slot
    CHECK(set.size() == 3);
    CHECK(set.find(OpKind::RemovePackage, L"RÉSUMÉ") == &set.operations()[1]);
    CHECK(set.find(OpKind::RemoveAppx, L"résumé") == &set.operations()[2]);
    CHECK(set.find(OpKind::RemovePackage, L"Resume") == nullptr); // one placeholder for é, but iequals decides
    set.add({OpKind::RemovePackage, L"RÉSUMÉ", L"v2"});           // replaces in place
    CHECK(set.size() == 3);
    CHECK(set.operations()[1].value == L"v2");
    CHECK(set.remove(OpKind::RemovePackage, L"çalışma~1"));
    CHECK(set.find(OpKind::RemovePackage, L"résumé") == &set.operations()[0]); // positions follow the erase
    CHECK(set.undo());
    CHECK(set.find(OpKind::RemovePackage, L"Çalışma~1") == &set.operations()[0]);
    const ChangeSet copy = set.withoutHistory();
    CHECK(copy.operations().size() == 3);
    CHECK_FALSE(copy.canUndo());
    CHECK(copy.find(OpKind::RemoveAppx, L"RÉSUMÉ") != nullptr);
}

TEST_CASE("audit: removeAll takes the slots out in one pass and keeps the order of the rest") {
    ChangeSet set;
    for (int i = 0; i < 10; ++i) {
        set.add({OpKind::RemovePackage, L"p" + std::to_wstring(i)});
    }
    CHECK(set.removeAll({{OpKind::RemovePackage, L"P3"}, {OpKind::RemovePackage, L"p7"}, {OpKind::RemovePackage, L"p3"},
                         {OpKind::RemoveAppx, L"p1"}, {OpKind::RemovePackage, L"none"}}) == 2);
    REQUIRE(set.size() == 8);
    const wchar_t* left[] = {L"p0", L"p1", L"p2", L"p4", L"p5", L"p6", L"p8", L"p9"};
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(set.operations()[i].target == left[i]);
        CHECK(set.find(OpKind::RemovePackage, left[i]) == &set.operations()[i]);
    }
    CHECK(set.undo()); // one step for the batch
    CHECK(set.size() == 10);
    CHECK(set.removeAll({{OpKind::RemovePackage, L"none"}}) == 0);
}

TEST_CASE("audit PG-04: a group of changes is one undo step") {
    ChangeSet set;
    set.add({OpKind::RemovePackage, L"before"});
    set.beginGroup();
    for (int i = 0; i < 50; ++i) {
        set.add({OpKind::RemoveCapability, L"cap" + std::to_wstring(i)});
    }
    set.remove(OpKind::RemoveCapability, L"cap0");
    set.endGroup();
    CHECK(set.size() == 50);
    CHECK(set.undo());
    CHECK(set.size() == 1); // the whole group
    CHECK(set.redo());
    CHECK(set.size() == 50);
    set.add({OpKind::RemovePackage, L"after"}); // outside a group: its own step again
    CHECK(set.undo());
    CHECK(set.size() == 50);
}

TEST_CASE("audit OPS-03: the undo history is bounded by size, the newest state always stays") {
    ChangeSet set;
    // What the welcome puts in the queue: one step of ~1 MB.
    set.add({OpKind::WriteFile, L"big", std::wstring(512 * 1024, L'x')});
    for (int i = 0; i < 300; ++i) {
        set.add({OpKind::RemovePackage, L"p" + std::to_wstring(i)});
    }
    int steps = 0;
    while (set.undo()) {
        ++steps;
    }
    CHECK(steps >= 1);
    CHECK(steps <= 64);  // 64 MB of ~1 MB states; it was 200 (200 MB)
    CHECK(set.size() >= 236); // never further back than what was kept

    ChangeSet small; // an ordinary queue keeps its 200 steps
    for (int i = 0; i < 300; ++i) {
        small.add({OpKind::RemovePackage, L"p" + std::to_wstring(i)});
    }
    steps = 0;
    while (small.undo()) {
        ++steps;
    }
    CHECK(steps == 200);
}
