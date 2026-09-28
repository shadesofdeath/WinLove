// P08: update package names → kind / KB / target / architecture, and the planner's servicing order.
#include "core/image/UpdatePackage.h"
#include "core/ops/Planner.h"

#include <doctest.h>

using namespace wl;
using core::UpdateKind;
using core::ops::OpKind;

TEST_CASE("analyzeUpdate reads Microsoft catalog file names") {
    const auto ssu = core::analyzeUpdate(LR"(C:\u\windows11.0-kb5043080-x64_ssu_7e8f.msu)");
    CHECK(ssu.kind == UpdateKind::Ssu);
    CHECK(ssu.kb == L"KB5043080");
    CHECK(ssu.targetWindows == 11);
    CHECK(ssu.architecture == L"x64");

    const auto lcu = core::analyzeUpdate(LR"(C:\u\windows11.0-kb5044284-x64_8d2c.msu)");
    CHECK(lcu.kind == UpdateKind::Lcu);
    const auto net = core::analyzeUpdate(LR"(C:\u\Windows11.0-KB5044030-x64-NDP481_a1b2.msu)");
    CHECK(net.kind == UpdateKind::DotNet);
    CHECK(net.kb == L"KB5044030");
    const auto old = core::analyzeUpdate(LR"(C:\u\windows10.0-kb5041585-arm64.msu)");
    CHECK(old.targetWindows == 10);
    CHECK(old.architecture == L"arm64");
    const auto langPack = core::analyzeUpdate(LR"(C:\u\Microsoft-Windows-Client-Language-Pack_x64_tr-tr.cab)");
    CHECK(langPack.kind == UpdateKind::Other);
    CHECK(langPack.targetWindows == 0);

    CHECK(core::isUpdateFile(LR"(x.MSU)"));
    CHECK_FALSE(core::isUpdateFile(LR"(x.iso)"));
    CHECK(core::windowsGeneration(26200) == 11);
    CHECK(core::windowsGeneration(19045) == 10);
    CHECK(core::updateKindFromKey(core::updateKindKey(UpdateKind::DotNet)) == UpdateKind::DotNet);
}

TEST_CASE("planner applies updates SSU → LCU → .NET → other, after removals and features") {
    core::ops::ChangeSet set;
    set.add({OpKind::AddPackage, L"net.msu", L"dotnet"});
    set.add({OpKind::AddPackage, L"lang.cab", L"other"});
    set.add({OpKind::AddPackage, L"lcu.msu", L"lcu"});
    set.add({OpKind::EnableFeature, L"WSL"});
    set.add({OpKind::AddPackage, L"ssu.msu", L"ssu"});
    const auto plan = core::ops::plan(set);
    REQUIRE(plan.steps.size() == 5);
    CHECK(plan.steps[0].operation.target == L"WSL");
    CHECK(plan.steps[1].operation.target == L"ssu.msu");
    CHECK(plan.steps[2].operation.target == L"lcu.msu");
    CHECK(plan.steps[3].operation.target == L"net.msu");
    CHECK(plan.steps[4].operation.target == L"lang.cab");
}
