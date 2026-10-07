#include "base/Utf8.h"
#include "core/image/dism/StoreShrink.h"
#include "core/ops/ChangeSet.h"
#include "core/ops/Planner.h"

#include <doctest.h>

#include <filesystem>

using namespace wl;
using namespace wl::core;

TEST_CASE("store shrink: what a running Windows loads from WinSxS stays, the rest goes (D-079)") {
    // The store's own folders, any case.
    for (const wchar_t* name : {L"Manifests", L"Catalogs", L"FileMaps", L"Fusion", L"InstallTemp", L"SettingsManifests", L"Temp"}) {
        CAPTURE(std::wstring(name));
        CHECK(keptInShrunkStore(name));
    }
    CHECK_FALSE(keptInShrunkStore(L"Backup"));
    // Side-by-side assemblies programs ask for, every architecture (names as 25H2 has them).
    CHECK(keptInShrunkStore(L"amd64_microsoft.windows.common-controls_6595b64144ccf1df_6.0.26100.7019_none_3e05a3b0e7e5e0ba"));
    CHECK(keptInShrunkStore(L"x86_microsoft.windows.common-controls_6595b64144ccf1df_5.82.26100.7019_none_83b5e0c5b2c4c0f8"));
    CHECK(keptInShrunkStore(L"x86_microsoft.windows.c..-controls.resources_6595b64144ccf1df_6.0.26100.1_tr-tr_4c2b6b9b7f0d3e21"));
    CHECK(keptInShrunkStore(L"arm64_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.26100.7019_none_0a5d4b4b5c3f2e11"));
    CHECK(keptInShrunkStore(L"amd64_microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.9635_none_08e2c157a83ed5da"));
    CHECK(keptInShrunkStore(L"X86_MICROSOFT.VC80.CRT_1FC8B3B9A1E18E3B_8.0.50727.9680_NONE_D08F1D4A44BCA1B9"));
    CHECK(keptInShrunkStore(L"amd64_microsoft.windows.isolationautomation_6595b64144ccf1df_6.0.26100.1_none_a1b2c3d4e5f60718"));
    // Their redirection policies; not a policy of anything else.
    CHECK(keptInShrunkStore(L"amd64_policy.9.0.microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.9635_none_2b6a2a1a8b3e2f10"));
    CHECK(keptInShrunkStore(L"x86_policy.6.0.microsoft.windows.common-controls_6595b64144ccf1df_6.0.26100.1_none_1a2b3c4d5e6f7081"));
    CHECK_FALSE(keptInShrunkStore(L"x86_policy.12.0.microsoft.vc120.crt_1fc8b3b9a1e18e3b_12.0.1_none_1a2b3c4d5e6f7081"));
    CHECK_FALSE(keptInShrunkStore(L"x86_policy.x.0.microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.1_none_1a2b3c4d5e6f7081"));
    // The servicing stack.
    CHECK(keptInShrunkStore(L"amd64_microsoft-windows-servicingstack_31bf3856ad364e35_10.0.26100.7015_none_3c5c1d0f5a0a1b2c"));
    CHECK(keptInShrunkStore(L"x86_microsoft-windows-s..ngstack-onecorebase_31bf3856ad364e35_10.0.26100.7015_none_1a2b"));
    CHECK(keptInShrunkStore(L"amd64_microsoft-windows-s..stack-msg.resources_31bf3856ad364e35_10.0.26100.1_tr-tr_1a2b"));
    // Everything else: components (mostly hard links into System32), .NET, other Win32 assemblies.
    CHECK_FALSE(keptInShrunkStore(L"amd64_microsoft-edge-webview_31bf3856ad364e35_10.0.26100.7019_none_1a2b3c4d5e6f7081"));
    CHECK_FALSE(keptInShrunkStore(L"amd64_microsoft-windows-shell32_31bf3856ad364e35_10.0.26100.7019_none_1a2b3c4d5e6f7081"));
    CHECK_FALSE(keptInShrunkStore(L"msil_system.web_b03f5f7f11d50a3a_4.0.15912.0_none_1a2b3c4d5e6f7081"));
    CHECK_FALSE(keptInShrunkStore(L"wow64_microsoft.windows.winhttp_31bf3856ad364e35_5.1.26100.1_none_1a2b3c4d5e6f7081"));
    // Names that only look alike.
    CHECK_FALSE(keptInShrunkStore(L"amd64"));
    CHECK_FALSE(keptInShrunkStore(L"sparc_microsoft.windows.common-controls_6595b64144ccf1df_6.0.1_none_1"));
    CHECK_FALSE(keptInShrunkStore(L"amd64_microsoft.windows.common-controls_0000000000000000_6.0.1_none_1"));
    CHECK_FALSE(keptInShrunkStore(L"manifests2"));
    CHECK_FALSE(keptInShrunkStore(L""));
}

TEST_CASE("store shrink: only a mounted Windows image's own store is touched") {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-store-shrink-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / L"Windows" / L"WinSxS");
    CHECK_FALSE(planStoreShrink(dir, false, {}).has_value()); // no registry hives: not an image
    CHECK_FALSE(shrinkComponentStore(dir, {}).has_value());
    CHECK_FALSE(planStoreShrink(dir / L"missing", false, {}).has_value());
    std::filesystem::remove_all(dir);
}

TEST_CASE("store shrink: the operation round-trips and runs after everything else") {
    StoreShrinkOptions options{L"WinSxS'i küçült"};
    const auto back = storeShrinkFromJson(storeShrinkToJson(options));
    REQUIRE(back);
    CHECK(*back == options);
    CHECK_FALSE(storeShrinkFromJson("[1]").has_value());
    CHECK_FALSE(storeShrinkFromJson("not json").has_value());
    const auto kind = ops::opKindFromKey("shrinkStore");
    REQUIRE(kind);
    CHECK(*kind == ops::OpKind::ShrinkStore);

    ops::ChangeSet changes;
    changes.add({ops::OpKind::ShrinkStore, L"component-store-shrink", utf8::toWide(storeShrinkToJson(options)), ops::Risk::High});
    changes.add({ops::OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\X::Y", L"dword:00000001", ops::Risk::Low});
    changes.add({ops::OpKind::CleanupImage, L"component-store", L"{\"resetBase\":true}", ops::Risk::Medium});
    changes.add({ops::OpKind::AddPackage, L"C:\\u\\windows11.0-kb1-x64.msu", L"lcu", ops::Risk::Low});
    const auto plan = ops::plan(changes);
    REQUIRE(plan.steps.size() == 4);
    CHECK(plan.steps.back().operation.kind == ops::OpKind::ShrinkStore);
    CHECK(plan.steps.back().phase == ops::Phase::Shrink);
    CHECK(plan.steps[plan.steps.size() - 2].phase == ops::Phase::Settings);
}
