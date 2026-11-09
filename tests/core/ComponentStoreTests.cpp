// D-059: reading what CBS packages own (core/image/ComponentStore). The names and the value layout
// are those of a real 25H2 image (tools/lab_scan_components.ps1, tools/analyze_cbs.py).
#include "base/Utf8.h"
#include "core/image/ComponentStore.h"
#include "core/image/DeepRemoval.h"
#include "core/image/SystemComponents.h"
#include "core/ops/ChangeSet.h"
#include "core/ops/Planner.h"

#include <doctest.h>

#include <string>
#include <vector>

using namespace wl;
using core::ComponentStoreIndex;

TEST_CASE("component store: a WinSxS folder or COMPONENTS key name loses its version, culture and hash") {
    CHECK(ComponentStoreIndex::componentName(
              L"amd64_microsoft-windows-mediaplayer-autoplay_31bf3856ad364e35_10.0.26100.7705_none_b69bb996f01a4062") ==
          L"amd64_microsoft-windows-mediaplayer-autoplay");
    CHECK(ComponentStoreIndex::componentName(L"wow64_microsoft-windows-d..ing-management-host_31bf3856ad364e35_10.0.26100.5074_tr-tr_c95f41ebd93325fd") ==
          L"wow64_microsoft-windows-d..ing-management-host");
    // Driver components have underscores of their own.
    CHECK(ComponentStoreIndex::componentName(L"amd64_dual_prnge001.inf_31bf3856ad364e35_10.0.26100.1_none_0123456789abcdef") ==
          L"amd64_dual_prnge001.inf");
    // Not a component folder (Manifests, Backup …): kept, lower case.
    CHECK(ComponentStoreIndex::componentName(L"Manifests") == L"manifests");
}

TEST_CASE("component store: the package an i!CBS_ deployment value names") {
    const std::string identity = "Package_4_for_KB5066128~31bf3856ad364e35~amd64~~10.0.9321.3";
    std::vector<std::uint8_t> data{static_cast<std::uint8_t>(identity.size()), 0, 0, 0, 1, 0, 0, 0};
    data.insert(data.end(), identity.begin(), identity.end());
    data.insert(data.end(), {'.', '9', 0, 0}); // the update name follows
    CHECK(ComponentStoreIndex::deploymentPackage(data) == std::wstring(identity.begin(), identity.end()));
    CHECK(ComponentStoreIndex::deploymentPackage({}).empty());
    CHECK(ComponentStoreIndex::deploymentPackage({200, 0, 0, 0, 1, 0, 0, 0, 'a'}).empty()); // longer than the data
}

TEST_CASE("component store: cumulative updates are not owners") {
    CHECK(ComponentStoreIndex::isUpdatePackage(L"Package_for_RollupFix"));
    CHECK(ComponentStoreIndex::isUpdatePackage(L"package_12_for_kb5066128"));
    CHECK(ComponentStoreIndex::isUpdatePackage(L"Package_for_ServicingStack_8035"));
    CHECK_FALSE(ComponentStoreIndex::isUpdatePackage(L"Microsoft-Windows-MediaPlayer-Package"));
    CHECK_FALSE(ComponentStoreIndex::isUpdatePackage(L"Packaged-Something"));
}

TEST_CASE("component store: child packages of a manifest") {
    const std::string mum = R"(<?xml version="1.0" encoding="utf-8"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v3" manifestVersion="1.0">
  <assemblyIdentity name="Microsoft-Windows-MediaPlayer-Package" version="10.0.26100.8036" processorArchitecture="amd64" />
  <package identifier="KB777778" releaseType="OnDemand Pack">
    <parent><assemblyIdentity name="Microsoft-Windows-Foundation-Package" /></parent>
    <update name="c20ec7495cdae0508b34c06f9c359358">
      <package contained="false" integrate="hidden">
        <assemblyIdentity name="Microsoft-Windows-MediaPlayer-Payload-Package" version="10.0.26100.8036" />
      </package>
      <component><assemblyIdentity name="Microsoft-Windows-MediaPlayer-Deployment" /></component>
    </update>
  </package>
</assembly>)";
    // Only <package><assemblyIdentity>: not the package itself, its parent or its components.
    CHECK(ComponentStoreIndex::manifestChildren(mum) == std::vector<std::wstring>{L"microsoft-windows-mediaplayer-payload-package"});
    CHECK(ComponentStoreIndex::manifestChildren("not xml").empty());
}

TEST_CASE("component store: exclusive bytes of a set of package trees") {
    ComponentStoreIndex index;
    index.addInstalled(L"Microsoft-Windows-MediaPlayer-Package");
    index.addChild(L"microsoft-windows-mediaplayer-package", L"microsoft-windows-mediaplayer-payload-package");
    index.addOwner(L"amd64_wmp", L"microsoft-windows-mediaplayer-payload-package"); // only the player
    index.addOwner(L"amd64_codecs", L"microsoft-windows-mediaplayer-payload-package");
    index.addOwner(L"amd64_codecs", L"microsoft-windows-client-features-package"); // shared: stays
    index.addOwner(L"amd64_wmp-wow64", L"microsoft-windows-mediaplayer-wow64-package");
    index.addBytes(L"amd64_wmp", 1000);
    index.addBytes(L"amd64_codecs", 50000);
    index.addBytes(L"amd64_wmp-wow64", 300);

    CHECK(index.installed(L"MICROSOFT-WINDOWS-MEDIAPLAYER-PACKAGE"));
    CHECK_FALSE(index.installed(L"Microsoft-Windows-Other-Package"));
    CHECK(index.exclusiveBytes({L"Microsoft-Windows-MediaPlayer-Package"}) == 1000); // through its child
    CHECK(index.exclusiveBytes({L"Microsoft-Windows-MediaPlayer-Package", L"Microsoft-Windows-MediaPlayer-WOW64-Package"}) == 1300);
    // Both owners inside the set: the shared component counts too.
    CHECK(index.exclusiveBytes({L"Microsoft-Windows-MediaPlayer-Package", L"Microsoft-Windows-Client-Features-Package"}) == 51000);
    CHECK(index.exclusiveBytes({L"Microsoft-Windows-Unknown-Package"}) == 0);
}

// ---- deep removal (D-060) ----------------------------------------------------------------------

TEST_CASE("deep removal: only legacy device classes, however the GUID is written") {
    CHECK(core::normalizeClassGuid(L" 4d36e96d-e325-11ce-bfc1-08002be10318 ") == L"{4D36E96D-E325-11CE-BFC1-08002BE10318}");
    CHECK(core::normalizeClassGuid(L"{4d36e96d-e325-11ce-bfc1-08002be10318}") == L"{4D36E96D-E325-11CE-BFC1-08002BE10318}");
    CHECK(core::normalizeClassGuid(L"{4d36e96d-e325-11ce-bfc1-08002be1031}").empty());
    CHECK(core::normalizeClassGuid(L"modem").empty());
    CHECK(core::isDeepRemovableClass(L"{4d36e96d-e325-11ce-bfc1-08002be10318}")); // Modem
    CHECK(core::isDeepRemovableClass(L"{4D36E980-E325-11CE-BFC1-08002BE10318}")); // FloppyDisk
    // Never: network, storage controllers, USB, keyboards, display, printers.
    for (const wchar_t* needed : {L"{4D36E972-E325-11CE-BFC1-08002BE10318}", L"{4D36E97B-E325-11CE-BFC1-08002BE10318}",
                                  L"{36FC9E60-C465-11CF-8056-444553540000}", L"{4D36E96B-E325-11CE-BFC1-08002BE10318}",
                                  L"{4D36E968-E325-11CE-BFC1-08002BE10318}", L"{4D36E979-E325-11CE-BFC1-08002BE10318}"}) {
        CAPTURE(std::wstring(needed));
        CHECK_FALSE(core::isDeepRemovableClass(needed));
    }
}

TEST_CASE("deep removal: the class GUID in a DriverPackages Version value") {
    // mdm3com.inf on 25H2: ff ff 09 00 00 00 00 00 | 6d e9 36 4d 25 e3 ce 11 bf c1 08 00 2b e1 03 18 | …
    const std::vector<std::uint8_t> version{0xff, 0xff, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6d, 0xe9, 0x36, 0x4d, 0x25, 0xe3,
                                            0xce, 0x11, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18, 0x00, 0x80};
    CHECK(core::driverPackageClass(version) == L"{4D36E96D-E325-11CE-BFC1-08002BE10318}");
    CHECK(core::driverPackageClass({1, 2, 3}).empty());
}

TEST_CASE("deep removal: a recipe carries its classes, and a preset cannot widen them") {
    core::ComponentRecipe recipe;
    recipe.title = L"Modem";
    recipe.driverClasses = {L"{4D36E96D-E325-11CE-BFC1-08002BE10318}"};
    CHECK(core::validateComponentRecipe(recipe));
    const auto back = core::componentRecipeFromJson(core::componentRecipeToJson(recipe));
    REQUIRE(back);
    CHECK(*back == recipe);
    recipe.driverClasses.push_back(L"{4D36E972-E325-11CE-BFC1-08002BE10318}"); // Net
    CHECK_FALSE(core::validateComponentRecipe(recipe));
}

TEST_CASE("deep removal runs after the updates in one Apply (an update needs the drivers' payload)") {
    using namespace core::ops;
    core::ComponentRecipe deep;
    deep.title = L"Modem";
    deep.driverClasses = {L"{4D36E96D-E325-11CE-BFC1-08002BE10318}"};
    core::ComponentRecipe plain;
    plain.title = L"Help";
    plain.packages = {L"Microsoft-Windows-Help-ClientUA-Client-Package"};
    ChangeSet changes;
    changes.add(Operation{OpKind::RemoveComponent, L"deep-modem", utf8::toWide(core::componentRecipeToJson(deep))});
    changes.add(Operation{OpKind::RemoveComponent, L"telemetry", utf8::toWide(core::componentRecipeToJson(plain))});
    changes.add(Operation{OpKind::AddPackage, L"lcu.msu", L"lcu"});
    const auto p = plan(changes);
    REQUIRE(p.steps.size() == 3);
    CHECK(p.steps[0].phase == Phase::Updates); // D-124: the update into the whole image first
    CHECK(p.steps[1].phase == Phase::Remove);  // the package-level component
    CHECK(p.steps[2].phase == Phase::DeepRemove);
}

// ---- D-113: the store split between reference-model definitions -------------------------------------

TEST_CASE("component store: an identity's name, match string and architecture (D-113)") {
    const auto id = ComponentStoreIndex::storeIdentity(
        "Microsoft-Windows-TaskScheduler-Service.Resources, Culture=tr-TR, Version=10.0.26100.1, "
        "PublicKeyToken=31bf3856ad364e35, ProcessorArchitecture=wow64, versionScope=NonSxS");
    CHECK(id.name == L"microsoft-windows-taskscheduler-service");
    CHECK(id.match == L"microsoft-windows-taskscheduler-service|31bf3856ad364e35|10.0.26100.1");
    CHECK(id.bits32);
    const auto native = ComponentStoreIndex::storeIdentity(
        "System.Web, Culture=neutral, Version=4.0.15920.100, PublicKeyToken=b03f5f7f11d50a3a, ProcessorArchitecture=amd64");
    CHECK(native.name == L"system.web");
    CHECK_FALSE(native.bits32);
}

TEST_CASE("component store: patterns see the name, or the name, token and version (D-113)") {
    const std::wstring web = L"system.web|b03f5f7f11d50a3a|4.0.15920.100";
    CHECK(ComponentStoreIndex::storePatternMatches(L"system.web*", web));
    CHECK(ComponentStoreIndex::storePatternMatches(L"system.web", web)); // exact name
    CHECK_FALSE(ComponentStoreIndex::storePatternMatches(L"system.we", web));
    CHECK(ComponentStoreIndex::storePatternMatches(L"*|b03f5f7f11d50a3a|*", web));
    CHECK(ComponentStoreIndex::storePatternMatches(L"*|b03f5f7f11d50a3a", web));
    CHECK(ComponentStoreIndex::storePatternMatches(L"system.*|b03f5f7f11d50a3a|4.*", web));
    CHECK_FALSE(ComponentStoreIndex::storePatternMatches(L"system.*|b03f5f7f11d50a3a|2.*", web));
    CHECK_FALSE(ComponentStoreIndex::storePatternMatches(L"*|31bf3856ad364e35|*", web));
    CHECK(ComponentStoreIndex::storePatternMatches(L"Microsoft-Windows-*", L"microsoft-windows-foo|31bf3856ad364e35|10.0.1"));
    // An exact name beats any glob; then the longer literal text.
    CHECK(ComponentStoreIndex::storePatternSpecificity(L"microsoft-windows-scripting-jscript") >
          ComponentStoreIndex::storePatternSpecificity(L"microsoft-windows-scripting-jscript9legacy*"));
    CHECK(ComponentStoreIndex::storePatternSpecificity(L"microsoft-windows-netfx-shared-wcf*") >
          ComponentStoreIndex::storePatternSpecificity(L"microsoft-windows-netfx*"));
}

TEST_CASE("component store: each component goes to its most specific definition, 32-bit apart (D-113)") {
    ComponentStoreIndex index;
    const char* tail = ", Culture=neutral, Version=10.0.1, PublicKeyToken=31bf3856ad364e35, ProcessorArchitecture=";
    const auto add = [&](const std::string& name, const char* arch, std::uint64_t bytes) {
        index.addStoreComponent(name + tail + arch, bytes);
    };
    add("Microsoft-Windows-TaskScheduler-Service", "amd64", 100);
    add("Microsoft-Windows-TaskScheduler-Service", "wow64", 10);
    add("Microsoft-Windows-TaskScheduler-Service.Resources", "amd64", 1); // merged into its name
    add("Microsoft-Windows-TaskScheduler-Compatibility", "amd64", 50);
    add("Microsoft-Windows-TaskScheduler-Compatibility", "x86", 5);
    add("Microsoft-Windows-Unrelated", "amd64", 999);
    const auto shares = index.shareStore({{L"microsoft-windows-taskscheduler-*"},          // the parent
                                          {L"microsoft-windows-taskscheduler-compatibility*"}, // its child
                                          {L"microsoft-windows-nothing-here*"}});
    REQUIRE(shares.size() == 3);
    CHECK(shares[0].names64 == std::vector<std::wstring>{L"microsoft-windows-taskscheduler-service"});
    CHECK(shares[0].names32 == std::vector<std::wstring>{L"microsoft-windows-taskscheduler-service"});
    CHECK(shares[0].bytes64 == 101);
    CHECK(shares[0].bytes32 == 10);
    CHECK(shares[1].names64 == std::vector<std::wstring>{L"microsoft-windows-taskscheduler-compatibility"});
    CHECK(shares[1].bytes64 == 50);
    CHECK(shares[1].bytes32 == 5);
    CHECK(shares[2].empty());
}

TEST_CASE("component recipe: store names, bits and services travel, and a preset cannot reach the core (D-113)") {
    core::ComponentRecipe recipe;
    recipe.title = L"Karakter Eşlemi";
    recipe.storeNames = {L"microsoft-windows-charmap"};
    recipe.storeBits = 32;
    recipe.services = {L"WebClient"};
    recipe.afterUpdates = true;
    CHECK(core::validateComponentRecipe(recipe));
    CHECK(*core::componentRecipeFromJson(core::componentRecipeToJson(recipe)) == recipe);

    auto kernel = recipe;
    kernel.storeNames = {L"microsoft-windows-kernel32"};
    CHECK_FALSE(core::validateComponentRecipe(kernel));
    kernel.storeNames = {L"microsoft-windows-terminalservices-localsessionmanager"};
    CHECK_FALSE(core::validateComponentRecipe(kernel));
    kernel.storeNames = {L"..\\windows"};
    CHECK_FALSE(core::validateComponentRecipe(kernel));
    auto mdac = recipe; // names the store really has
    mdac.storeNames = {L"microsoft-windows-microsoft-data-access-components-(mdac)-ado15-dll",
                       L"microsoft-windows-base technologies-debuggertransports"};
    CHECK(core::validateComponentRecipe(mdac));
    mdac.storeNames = {L" microsoft-windows-charmap"};
    CHECK_FALSE(core::validateComponentRecipe(mdac));
    kernel.storeNames = {L"Microsoft-Windows-Charmap"}; // names are lower case, as the store gives them
    CHECK_FALSE(core::validateComponentRecipe(kernel));
    auto service = recipe;
    service.services = {L"RpcSs"};
    CHECK_FALSE(core::validateComponentRecipe(service));
    service.services = {L"Winmgmt"};
    CHECK_FALSE(core::validateComponentRecipe(service));
    service.services = {L"a\\b"};
    CHECK_FALSE(core::validateComponentRecipe(service));
    auto bits = recipe;
    bits.storeBits = 16;
    CHECK_FALSE(core::validateComponentRecipe(bits));
    CHECK(core::isProtectedStoreName(L"microsoft-windows-shell32"));
    CHECK_FALSE(core::isProtectedStoreName(L"microsoft-windows-charmap"));
    // D-120: what the reference tool keeps out of its removals (VM 2026-11-08: Explorer stopped after sign-in
    // once Accessibility, Pen and ink and Windows Insider had taken these).
    for (const wchar_t* name : {L"microsoft-onecore-accessibility-experience", L"microsoft-windows-tabletpc-platform-input-core",
                                L"microsoft-windows-tabletpc-platform-input-service-client", L"microsoft-windows-flighting-featureconfiguration-featureinventory-osclient",
                                L"microsoft-windows-flighting-settings", L"microsoft-windows-flighting-onesettingsclient-tasks",
                                L"microsoft-windows-errorreportingfaults", L"microsoft-windows-securestartup-filterdriver"}) {
        CAPTURE(std::wstring(name));
        CHECK(core::isProtectedStoreName(name));
    }
    // The rest of those components still goes.
    for (const wchar_t* name : {L"microsoft-windows-accessibilitycpl", L"microsoft-windows-tabletpc-legacyinkingcomponents",
                                L"microsoft-windows-flighting-featuretuning", L"microsoft-windows-errorreportingconsole",
                                L"microsoft-windows-securestartup-core", L"microsoft-windows-ui-xaml-maps"}) {
        CAPTURE(std::wstring(name));
        CHECK_FALSE(core::isProtectedStoreName(name));
    }
    CHECK(core::isProtectedService(L"TextInputManagementService"));
    // D-121: the reference tool's "basic functionality" services; the volume filters would leave Windows unbootable.
    for (const wchar_t* name : {L"fvevol", L"rdyboost", L"wlidsvc", L"AppReadiness", L"DeviceAssociationService", L"Wdf01000"}) {
        CAPTURE(std::wstring(name));
        CHECK(core::isProtectedService(name));
    }
    CHECK_FALSE(core::isProtectedService(L"TokenBroker")); // it goes only with its own files (`aad`), which guards hold
    // An older preset's ASP.NET recipe named System.Web (D-117); PowerShell's JSON needs it.
    CHECK(core::isProtectedStoreName(L"system.web"));
    CHECK(core::isProtectedStoreName(L"System.Web.Extensions"));
    CHECK_FALSE(core::isProtectedStoreName(L"system.web.mobile"));
    CHECK_FALSE(core::isProtectedStoreName(L"system.web.entity.design"));
}
