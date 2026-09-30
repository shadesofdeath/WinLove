// Removing a provisioned app without DISM, and patching Setup's boot image: what can be checked
// without an image. The names below are the ones in the 25H2 test image's registry; the removal
// itself runs on a mounted copy (tools/lab_appx.ps1, tools/lab_boot.ps1 — admin).
#include "core/image/BootImage.h"
#include "core/image/dism/Appx.h"

#include <doctest.h>

#include <algorithm>

using namespace wl;
using namespace wl::core;

namespace {
const wchar_t* const kStore = L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Appx\\AppxAllUserStore";

bool hasKey(const ComponentRecipe& recipe, RegistryWrite::Kind kind, const std::wstring& key) {
    return std::ranges::any_of(recipe.registry, [&](const RegistryWrite& w) { return w.kind == kind && w.key == key; });
}
} // namespace

TEST_CASE("appx: the family name of a package full name") {
    CHECK(appxFamilyName(L"Microsoft.SecHealthUI_1000.26100.8036.0_x64__8wekyb3d8bbwe") == L"Microsoft.SecHealthUI_8wekyb3d8bbwe");
    CHECK(appxFamilyName(L"Microsoft.DesktopAppInstaller_2025.926.104.0_neutral_~_8wekyb3d8bbwe") ==
          L"Microsoft.DesktopAppInstaller_8wekyb3d8bbwe");
    CHECK(appxFamilyName(L"Microsoft.BingNews_1.0.2.0_neutral_split.scale-125_8wekyb3d8bbwe") == L"Microsoft.BingNews_8wekyb3d8bbwe");
    // Not full names, or names that would leave the folder they are put under.
    CHECK(appxFamilyName(L"Microsoft.SecHealthUI_8wekyb3d8bbwe").empty());
    CHECK(appxFamilyName(L"").empty());
    CHECK(appxFamilyName(L"..\\..\\Windows_1_x64__abc").empty());
    CHECK(appxFamilyName(L"A_1_x64__b/c").empty());
    CHECK(appxFamilyName(L"A_1_x64__").empty());
}

TEST_CASE("appx: the native removal does what DISM's does — folders of the family, license, three keys") {
    const std::wstring bundle = L"Microsoft.DesktopAppInstaller_2025.926.104.0_neutral_~_8wekyb3d8bbwe";
    const ComponentRecipe recipe = appxRemovalRecipe(
        bundle, {L"Microsoft.DesktopAppInstaller_1.26.509.0_x64__8wekyb3d8bbwe", bundle,
                 // Not this family: must not ride along, whatever the registry says.
                 L"Microsoft.UI.Xaml.2.8_8.2511.26001.0_x64__8wekyb3d8bbwe", L"..\\..\\Windows"});
    CHECK(recipe.title == L"Microsoft.DesktopAppInstaller");
    CHECK(recipe.packages.empty());
    CHECK(recipe.paths == std::vector<std::wstring>{
                              L"Program Files\\WindowsApps\\" + bundle,
                              L"Program Files\\WindowsApps\\Microsoft.DesktopAppInstaller_1.26.509.0_x64__8wekyb3d8bbwe",
                              L"ProgramData\\Microsoft\\Windows\\ClipSVC\\Install\\Apps\\microsoft.desktopappinstaller_8wekyb3d8bbwe.xml"});
    REQUIRE(recipe.registry.size() == 3);
    CHECK(hasKey(recipe, RegistryWrite::Kind::DeleteKey, std::wstring(kStore) + L"\\Applications\\" + bundle));
    CHECK(hasKey(recipe, RegistryWrite::Kind::DeleteKey, std::wstring(kStore) + L"\\Staged\\Microsoft.DesktopAppInstaller_8wekyb3d8bbwe"));
    CHECK(hasKey(recipe, RegistryWrite::Kind::CreateKey,
                 std::wstring(kStore) + L"\\Deprovisioned\\Microsoft.DesktopAppInstaller_8wekyb3d8bbwe"));
    // It is a recipe like any other: the same checks guard it.
    CHECK(validateComponentRecipe(recipe).has_value());

    // Nothing staged (the registry key is gone already): the app's own folder is still named.
    const ComponentRecipe bare = appxRemovalRecipe(L"Microsoft.SecHealthUI_1000.26100.8036.0_x64__8wekyb3d8bbwe", {});
    CHECK(bare.paths.size() == 2);
    CHECK(bare.paths.front() == L"Program Files\\WindowsApps\\Microsoft.SecHealthUI_1000.26100.8036.0_x64__8wekyb3d8bbwe");

    // Not a package name: no recipe at all.
    const ComponentRecipe none = appxRemovalRecipe(L"Windows\\System32", {});
    CHECK(none.title.empty());
    CHECK(none.paths.empty());
    CHECK(none.registry.empty());
}

TEST_CASE("boot image: the checks that are on become LabConfig values; an empty patch does nothing") {
    BootPatch patch;
    CHECK(patch.empty());
    CHECK(patch.labConfigValues().empty());
    patch.bypassTpm = true;
    patch.bypassStorage = true;
    CHECK_FALSE(patch.empty());
    CHECK(patch.labConfigValues() == std::vector<std::wstring>{L"BypassTPMCheck", L"BypassStorageCheck"});

    BootPatch drivers;
    drivers.drivers.push_back(L"D:\\drivers\\iaStorVD.inf");
    CHECK_FALSE(drivers.empty());
    CHECK(drivers.labConfigValues().empty());

    const auto missing = setupImageIndex(std::filesystem::temp_directory_path() / L"wl-tests" / L"no-boot.wim");
    CHECK_FALSE(missing.has_value());
}
