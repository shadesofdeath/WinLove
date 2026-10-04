// D-066: Microsoft Store answers (trimmed copies of what the services returned on 2026-10-05) →
// search results, product, packages, file addresses, and what goes into an x64 image.
#include "core/store/MsStore.h"

#include <doctest.h>

#include <string>

using namespace wl;
using namespace wl::core;

namespace {

// One NewUpdates entry + its ExtendedUpdateInfo entry, as SyncUpdates sends them (Xml escaped).
std::string update(int id, const char* updateId, bool framework, const char* identity, const char* fileGuid, const char* ext,
                   const char* fullName, const char* sha256, int size) {
    std::string info = "<UpdateInfo><ID>" + std::to_string(id) + "</ID><Deployment><ID>1</ID></Deployment><IsLeaf>true</IsLeaf><Xml>"
                       "&lt;UpdateIdentity UpdateID=\"" + updateId + "\" RevisionNumber=\"1\" /&gt;&lt;Properties UpdateType=\"Software\" " +
                       (framework ? "IsAppxFramework=\"true\" " : "") + "/&gt;</Xml><Verification Algorithm=\"SHA256\"/></UpdateInfo>";
    std::string ext2 = "<Update><ID>" + std::to_string(id) + "</ID><Xml>&lt;ExtendedProperties PackageIdentityName=\"" + identity +
                       "\" /&gt;&lt;Files&gt;&lt;File FileName=\"" + fileGuid + ext + "\" Digest=\"x\" Size=\"" + std::to_string(size) +
                       "\" InstallerSpecificIdentifier=\"" + fullName + "\"&gt;&lt;AdditionalDigest Algorithm=\"SHA256\"&gt;" + sha256 +
                       "&lt;/AdditionalDigest&gt;&lt;/File&gt;&lt;File FileName=\"Abm_" + fileGuid +
                       ".cab\" Size=\"10\" /&gt;&lt;/Files&gt;&lt;HandlerSpecificData type=\"appx:AppxInstaller\"&gt;&lt;AppxPackageInstallData "
                       "PackageFileName=\"" + fileGuid + ext + "\" MainPackage=\"true\" /&gt;&lt;/HandlerSpecificData&gt;</Xml></Update>";
    return info + "\x01" + ext2;
}

std::string syncReply() {
    // SHA-256 of nothing in particular, base64: 32 bytes 0x00..0x1f.
    const char* sha = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
    const std::string parts[] = {
        update(1, "aaaa-1", false, "5319275A.WhatsAppDesktop", "g1", ".msixbundle", "5319275A.WhatsAppDesktop_2.2638.102.0_neutral_~_cv1g1gvanyjgm", sha, 300),
        update(2, "aaaa-2", false, "5319275A.WhatsAppDesktop", "g2", ".msixbundle", "5319275A.WhatsAppDesktop_2.2637.100.0_neutral_~_cv1g1gvanyjgm", sha, 299),
        update(3, "aaaa-3", true, "Microsoft.VCLibs.140.00", "g3", ".appx", "Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe", sha, 9),
        update(4, "aaaa-4", true, "Microsoft.VCLibs.140.00", "g4", ".appx", "Microsoft.VCLibs.140.00_14.0.33519.0_arm64__8wekyb3d8bbwe", sha, 9),
        update(5, "aaaa-5", true, "Microsoft.VCLibs.140.00", "g5", ".appx", "Microsoft.VCLibs.140.00_14.0.30035.0_x64__8wekyb3d8bbwe", sha, 8),
        update(6, "aaaa-6", false, "WikimediaFoundation.Wikipedia", "g6", ".emsixbundle", "WikimediaFoundation.Wikipedia_1.0.1.70_neutral_~_54ggd3ev8bvz6", sha, 7),
    };
    std::string infos, exts;
    for (const auto& p : parts) {
        const auto cut = p.find('\x01');
        infos += p.substr(0, cut);
        exts += p.substr(cut + 1);
    }
    return R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Body><SyncUpdatesResponse xmlns="http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService"><SyncUpdatesResult><NewUpdates>)" +
           infos + "</NewUpdates><ExtendedUpdateInfo><Updates>" + exts +
           "</Updates></ExtendedUpdateInfo></SyncUpdatesResult></SyncUpdatesResponse></s:Body></s:Envelope>";
}

} // namespace

TEST_CASE("Store: search results and the product") {
    const auto results = parseStoreSearch(
        R"({"Data":[{"PackageIdentifier":"9NKSQGP7F2NH","PackageName":"WhatsApp","Publisher":"WhatsApp Inc."},)"
        R"({"PackageIdentifier":"Mozilla.Firefox","PackageName":"Firefox","Publisher":"Mozilla"}]})");
    REQUIRE(results);
    REQUIRE(results->size() == 1); // a winget id is no Store app
    CHECK((*results)[0].name == L"WhatsApp");
    CHECK_FALSE(parseStoreSearch("<html>"));

    const auto product = parseStoreProduct(
        R"({"Product":{"ProductId":"9NKSQGP7F2NH","LocalizedProperties":[{"ProductTitle":"WhatsApp","PublisherName":"WhatsApp Inc."}],)"
        R"("DisplaySkuAvailabilities":[{"Sku":{"Properties":{"FulfillmentData":{"WuCategoryId":"3dadc9b1-3603-496c-a6d1-bf2fda81df89",)"
        R"("PackageFamilyName":"5319275A.WhatsAppDesktop_cv1g1gvanyjgm"},"Packages":[{"MaxDownloadSizeInBytes":355799980}]}}}]}})");
    REQUIRE(product);
    CHECK(product->wuCategoryId == L"3dadc9b1-3603-496c-a6d1-bf2fda81df89");
    CHECK(product->packageFamilyName == L"5319275A.WhatsAppDesktop_cv1g1gvanyjgm");
    CHECK(product->size == 355799980);
    // A Win32 product of the Store has no Windows Update category.
    CHECK_FALSE(parseStoreProduct(R"({"Product":{"ProductId":"XP8C9QZMS2PC1T","DisplaySkuAvailabilities":[]}})"));
}

TEST_CASE("Store: packages on Windows Update and what an x64 image takes") {
    const auto all = parseSyncUpdates(syncReply());
    REQUIRE(all);
    REQUIRE(all->size() == 6);
    const auto& first = (*all)[0];
    CHECK(first.fullName == L"5319275A.WhatsAppDesktop_2.2638.102.0_neutral_~_cv1g1gvanyjgm");
    CHECK(first.extension == L".msixbundle");
    CHECK(first.updateId == L"aaaa-1");
    CHECK(first.sha256 == L"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    CHECK(first.version() == L"2.2638.102.0");
    CHECK(first.architecture() == L"neutral");
    CHECK(first.fileName() == L"5319275A.WhatsAppDesktop_2.2638.102.0_neutral_~_cv1g1gvanyjgm.msixbundle");
    CHECK((*all)[2].framework);

    const auto x64 = pickStorePackages(*all, L"5319275A.WhatsAppDesktop_cv1g1gvanyjgm", L"x64");
    REQUIRE(x64.size() == 2); // the newest bundle + the newest x64 VCLibs
    CHECK(x64[0].version() == L"2.2638.102.0");
    CHECK(x64[1].fullName == L"Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe");
    const auto arm = pickStorePackages(*all, L"5319275A.WhatsAppDesktop_cv1g1gvanyjgm", L"arm64");
    REQUIRE(arm.size() == 2);
    CHECK(arm[1].architecture() == L"arm64");
    // Only an encrypted package (Store DRM): nothing that can go into an image.
    CHECK(pickStorePackages(*all, L"WikimediaFoundation.Wikipedia_54ggd3ev8bvz6", L"x64").empty());

    CHECK(parseFileUrls(R"(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Body><R><FileLocations><FileLocation><Url>http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/x?P1=1&amp;P2=2</Url></FileLocation></FileLocations></R></s:Body></s:Envelope>)") ==
          std::vector<std::wstring>{L"http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/x?P1=1&P2=2"});
    CHECK(trustedStoreUrl(L"http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/x"));
    CHECK_FALSE(trustedStoreUrl(L"http://microsoft.com@evil.example/x"));
    CHECK_FALSE(trustedStoreUrl(L"https://store.rg-adguard.net/x"));
}
