// D-059: reading what CBS packages own (core/image/ComponentStore). The names and the value layout
// are those of a real 25H2 image (tools/lab_scan_components.ps1, tools/analyze_cbs.py).
#include "core/image/ComponentStore.h"

#include <doctest.h>

#include <string>

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
