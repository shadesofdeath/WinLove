// D-093: the Store apps of a UUP set — the app database (a trimmed copy of
// DesktopTargetCompDB_App_Neutral of 26100, as the 26300.9550 set carries it), which edition gets
// what, which packages an architecture needs, and the files matched by hash.
#include "core/uup/UupApps.h"

#include <doctest.h>

using namespace wl::core::uup;

namespace {

// PayloadHash "AAEC…" = bytes 00 01 02 …: the hex form is easy to check.
constexpr const char* kDb = R"X(<?xml version="1.0" encoding="utf-8"?>
<CompDB Product="Desktop" BuildInfo="ge_release.26100.1" xmlns="http://schemas.microsoft.com/embedded/2004/10/ImageUpdate">
  <Features>
    <Feature Type="MSIXFramework" FeatureID="Microsoft.VCLibs.140.00_8wekyb3d8bbwe" FMID="MSDN" Group="PreinstalledApps">
      <Packages>
        <Package ID="Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe" PackageType="MSIXFrameworkPackage" />
        <Package ID="Microsoft.VCLibs.140.00_14.0.30704.0_arm64__8wekyb3d8bbwe" PackageType="MSIXFrameworkPackage" />
      </Packages>
    </Feature>
    <Feature Type="MSIXBundle" FeatureID="Microsoft.WindowsStore_8wekyb3d8bbwe" FMID="MSDN" Group="PreinstalledApps">
      <Dependencies>
        <Feature FeatureID="Microsoft.VCLibs.140.00_8wekyb3d8bbwe" Type="Required" />
      </Dependencies>
      <CustomInformation>
        <CustomInfo Key="licensedata"><![CDATA[<License ID="x"><PFM>microsoft.windowsstore_8wekyb3d8bbwe</PFM></License>]]></CustomInfo>
      </CustomInformation>
      <Packages>
        <Package ID="Microsoft.WindowsStore_2024.401.616.0_neutral_~_8wekyb3d8bbwe" PackageType="MSIXBundlePackage" />
        <Package ID="Microsoft.WindowsStore_22401.1400.6.0_x64__8wekyb3d8bbwe" PackageType="MSIXPackage" />
        <Package ID="Microsoft.WindowsStore_22401.1400.6.0_neutral_split.language-tr_8wekyb3d8bbwe" PackageType="MSIXResourcePackage" />
      </Packages>
    </Feature>
    <Feature Type="MSIXBundle" FeatureID="Microsoft.ZuneMusic_8wekyb3d8bbwe" FMID="MSDN" Group="PreinstalledApps">
      <Packages>
        <Package ID="Microsoft.ZuneMusic_2024.1.1.0_neutral_~_8wekyb3d8bbwe" PackageType="MSIXBundlePackage" />
      </Packages>
    </Feature>
    <Feature Type="MSIXBundle" FeatureID="Microsoft.Whiteboard_8wekyb3d8bbwe" FMID="MSDN" Group="PreinstalledApps">
      <Packages>
        <Package ID="Microsoft.Whiteboard_2024.1.1.0_neutral_~_8wekyb3d8bbwe" PackageType="MSIXBundlePackage" />
      </Packages>
    </Feature>
  </Features>
  <Packages>
    <Package ID="Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe" Version="14.0.30704.0">
      <Payload><PayloadItem PayloadHash="AAECAw==" PayloadSize="859530" Path="UUP\Desktop\Apps\IPA\WinStore\Microsoft.VCLibs.x64.14.00.appx" PayloadType="Canonical" /></Payload>
    </Package>
    <Package ID="Microsoft.WindowsStore_2024.401.616.0_neutral_~_8wekyb3d8bbwe" Version="2024.401.616.0">
      <Payload><PayloadItem PayloadHash="BAUGBw==" PayloadSize="8523" Path="UUP\Desktop\Apps\IPA\WinStore\Microsoft.WindowsStore_8wekyb3d8bbwe.msixbundle" PayloadType="Canonical" /></Payload>
    </Package>
    <Package ID="Microsoft.WindowsStore_22401.1400.6.0_x64__8wekyb3d8bbwe" Version="22401.1400.6.0">
      <Payload><PayloadItem PayloadHash="CAkKCw==" PayloadSize="24813268" Path="UUP\Desktop\Apps\IPA\WinStore\StorePackage_22401.1400.6.0_x64.msix" PayloadType="Canonical" /></Payload>
    </Package>
  </Packages>
</CompDB>)X";

File available(std::wstring name, std::wstring sha256, std::uint64_t size) {
    File f;
    f.name = std::move(name);
    f.sha256 = std::move(sha256);
    f.size = size;
    f.url = L"http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/x";
    return f;
}

} // namespace

TEST_CASE("UUP apps: the database") {
    const auto all = parseAppCompDb(kDb);
    REQUIRE(all);
    REQUIRE(all->size() == 4);
    const auto& vclibs = (*all)[0];
    CHECK(vclibs.framework);
    CHECK(vclibs.packages.size() == 2);
    CHECK(vclibs.packages[0].fileName == L"Microsoft.VCLibs.x64.14.00.appx");
    CHECK(vclibs.packages[0].sha256 == L"00010203");
    CHECK(vclibs.packages[0].size == 859530);
    const auto& store = (*all)[1];
    CHECK_FALSE(store.framework);
    CHECK(store.dependencies == std::vector<std::wstring>{L"Microsoft.VCLibs.140.00_8wekyb3d8bbwe"});
    CHECK(store.license.find("microsoft.windowsstore_8wekyb3d8bbwe") != std::string::npos);
    REQUIRE(store.packages.size() == 3);
    CHECK(store.packages[0].bundle);
    CHECK(store.packages[0].fileName == L"Microsoft.WindowsStore_8wekyb3d8bbwe.msixbundle");
    CHECK(store.packages[1].sha256 == L"08090a0b");
    CHECK(store.packages[2].fileName.empty()); // no payload listed: not in this trimmed copy
    CHECK_FALSE(parseAppCompDb("not xml"));
}

TEST_CASE("UUP apps: what an edition gets") {
    CHECK(appForEdition(L"Microsoft.WindowsStore_8wekyb3d8bbwe", L"Professional"));
    CHECK(appForEdition(L"Microsoft.ZuneMusic_8wekyb3d8bbwe", L"Professional"));
    CHECK_FALSE(appForEdition(L"Microsoft.ZuneMusic_8wekyb3d8bbwe", L"ProfessionalN"));
    CHECK_FALSE(appForEdition(L"Microsoft.HEVCVideoExtension_8wekyb3d8bbwe", L"CoreN"));
    CHECK(appForEdition(L"Microsoft.WindowsStore_8wekyb3d8bbwe", L"CoreN"));
    CHECK_FALSE(appForEdition(L"Microsoft.Whiteboard_8wekyb3d8bbwe", L"Professional"));
    CHECK(appForEdition(L"Microsoft.Whiteboard_8wekyb3d8bbwe", L"PPIPro"));
    CHECK(appForEdition(L"Microsoft.ZuneMusic_8wekyb3d8bbwe", L"PROFESSIONAL")); // UUP's code, not N
    CHECK_FALSE(appForEdition(L"Microsoft.ZuneMusic_8wekyb3d8bbwe", L"PROFESSIONALN"));
}

TEST_CASE("UUP apps: packages of an architecture") {
    CHECK(packageForArchitecture(L"Microsoft.WindowsStore_22401.1400.6.0_x64__8wekyb3d8bbwe", L"amd64"));
    CHECK(packageForArchitecture(L"Microsoft.VCLibs.140.00_14.0.30704.0_x86__8wekyb3d8bbwe", L"amd64"));
    CHECK_FALSE(packageForArchitecture(L"Microsoft.VCLibs.140.00_14.0.30704.0_arm64__8wekyb3d8bbwe", L"amd64"));
    CHECK(packageForArchitecture(L"Microsoft.WindowsStore_2024.401.616.0_neutral_~_8wekyb3d8bbwe", L"amd64"));
    CHECK(packageForArchitecture(L"Microsoft.VCLibs.140.00_14.0.30704.0_arm64__8wekyb3d8bbwe", L"arm64"));
    CHECK(packageForArchitecture(L"Microsoft.VCLibs.140.00_14.0.30704.0_x64__8wekyb3d8bbwe", L"arm64"));
}

TEST_CASE("UUP apps: the plan and its files") {
    const auto all = parseAppCompDb(kDb);
    REQUIRE(all);
    const auto plan = appsFor(*all, {L"Professional"}, L"amd64");
    REQUIRE(plan.size() == 3); // VCLibs (first: a framework), Store, Music; not Whiteboard
    CHECK(plan[0].framework);
    CHECK(plan[0].packages.size() == 1); // the arm64 one left out
    CHECK(plan[1].id == L"Microsoft.WindowsStore_8wekyb3d8bbwe");
    CHECK(plan[2].id == L"Microsoft.ZuneMusic_8wekyb3d8bbwe");
    CHECK(appsFor(*all, {L"ProfessionalN"}, L"amd64").size() == 2); // no Music on N

    const std::vector<File> set{available(L"Microsoft.VCLibs.x64.14.00.appx", L"00010203", 859530),
                                available(L"StorePackage_x64.msix", L"08090a0b", 24813268),
                                available(L"Store.msixbundle", L"04050607", 8523)};
    std::vector<std::wstring> missing;
    const auto files = appFiles(plan, set, &missing);
    REQUIRE(files.size() == 3);
    CHECK(files[0].name == L"Frameworks\\Microsoft.VCLibs.x64.14.00.appx");
    CHECK(files[1].name == L"Microsoft.WindowsStore_8wekyb3d8bbwe\\Microsoft.WindowsStore_8wekyb3d8bbwe.msixbundle");
    CHECK(files[2].name == L"Microsoft.WindowsStore_8wekyb3d8bbwe\\StorePackage_22401.1400.6.0_x64.msix");
    CHECK(files[1].kind == FileKind::App);
    // The resource package with no payload and Music's bundle (not in the set) are missing.
    CHECK(missing.size() == 2);
}
