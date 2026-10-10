// D-093: what a UUP set's update packages are (from their update.mum and file list, the way the
// 26300.9550 set of 2026-10-09 has them), and the ISO label.
#include "core/uup/UupConvert.h"

#include <doctest.h>

using namespace wl::core::uup;

TEST_CASE("UUP updates: roles from the package's content") {
    // Every update.mum mentions a servicing stack: not a reason to call one an SSU.
    const wchar_t* mum = L"<assembly><package identifier=\"KB5121794\" releaseType=\"Update\"><parent><assemblyIdentity "
                         L"name=\"Microsoft-Windows-CoreEdition\"/></parent><servicingStack/></package></assembly>";
    CHECK(updateRoleFrom(L"Windows11.0-KB5121794-x64.cab", true, mum,
                         L"update.cat\nMicrosoft-Windows-Ge-Client-Server-26300-Version-Enablement-Package~31bf3856ad364e35~amd64~~"
                         L"10.0.26100.9544.cat\n") == UpdateRole::Enablement);
    CHECK(updateRoleFrom(L"Windows11.0-KB5126052-x64-NDP481.cab", true, mum, L"yellowcorner.gif\nalert_lrg.gif\n") ==
          UpdateRole::DotNet);
    CHECK(updateRoleFrom(L"Windows11.0-KB5125758-x64.cab", true,
                         L"<assemblyIdentity name=\"Package_for_SafeOSDU\"/><servicingStack/>", L"dbxupdatelegacy.cab\n") ==
          UpdateRole::SafeOs);
    CHECK(updateRoleFrom(L"Windows11.0-KB5127216-x64.cab", false, L"", L"setup.exe\nsetuphost.exe\n") == UpdateRole::SetupDu);
    CHECK(updateRoleFrom(L"Windows11.0-KB5099999-x64.cab", true, L"<assemblyIdentity name=\"Package_for_RollupFix\"/>", L"") ==
          UpdateRole::Cumulative);
    CHECK(updateRoleFrom(L"SSU-26100.9540-x64.cab", true, mum,
                         L"amd64_microsoft-windows-servicingstack_31bf3856ad364e35_10.0.26100.9540_none_x.manifest\n") ==
          UpdateRole::ServicingStack);
    CHECK(updateRoleFrom(L"Windows11.0-KB5043080-x64.msu", true, L"", L"") == UpdateRole::Checkpoint);
    CHECK(updateRoleFrom(L"Windows11.0-KB5124010-x64.msu", true, L"", L"") == UpdateRole::Cumulative);
    CHECK(updateRoleFrom(L"Windows11.0-KB5126052-x64-ndp481.msu", true, L"", L"") == UpdateRole::DotNet);
    CHECK(updateRoleFrom(L"Windows11.0-KB5000001-x64.cab", true, mum, L"amd64_something_else.manifest\n") == UpdateRole::Other);
}

TEST_CASE("UUP: the ISO label says edition, architecture and language") {
    UupSetFiles set;
    UupEdition pro;
    pro.editionId = L"Professional";
    pro.architecture = L"x64";
    pro.language = L"tr-TR";
    set.editions.push_back(pro);
    CHECK(isoLabel(set) == L"CPRA_X64FRE_TR-TR_DV9");
    UupEdition home = pro;
    home.editionId = L"Core";
    set.editions.push_back(home);
    CHECK(isoLabel(set) == L"CCSA_X64FRE_TR-TR_DV9");
    set.editions.resize(1);
    set.editions[0].architecture = L"arm64";
    set.editions[0].language = L"en-us";
    CHECK(isoLabel(set) == L"CPRA_A64FRE_EN-US_DV9");
}

TEST_CASE("UUP: Windows 10's ESU-era cumulative update refusing an offline image is recognised") {
    CHECK(esuRefusal(19041, static_cast<std::int32_t>(0x80073713)));
    CHECK_FALSE(esuRefusal(26100, static_cast<std::int32_t>(0x80073713))); // Windows 11: a real failure
    CHECK_FALSE(esuRefusal(19041, static_cast<std::int32_t>(0x800F0823)));  // the servicing stack: our bug, not ESU
    CHECK_FALSE(esuRefusal(0, static_cast<std::int32_t>(0x80073713)));
}
