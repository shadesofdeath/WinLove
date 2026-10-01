// D-046: Microsoft Update Catalog pages → entries → offers. The pages are trimmed copies of what
// the catalog returned on 2026-09-30 (row markup as served).
#include "core/net/Http.h"
#include "core/updates/UpdateCatalog.h"

#include <doctest.h>

#include <string>

using namespace wl;
using namespace wl::core;

namespace {

std::string row(const char* id, int r, const char* title, const char* products, const char* classification,
                const char* date, const char* size) {
    std::string s = "\t</tr><tr id=\"" + std::string(id) + "_R" + std::to_string(r) + "\" style=\"border-width:0px;\">\n";
    auto cell = [&](int c, const std::string& body) {
        s += "<td class=\"resultsbottomBorder resultspadding\" id=\"" + std::string(id) + "_C" + std::to_string(c) + "_R" +
             std::to_string(r) + "\">" + body + "</td>";
    };
    cell(0, "");
    cell(1, "\n <a id='" + std::string(id) + "_link' href= \"javascript:void(0);\" onclick='goToDetails(\"" + id +
                "\");' class=\"contentTextItemSpacerNoBreakLink\">\n                            " + title +
                "\n                        </a>\n");
    cell(2, std::string("\n   ") + products + "\n ");
    cell(3, std::string("\n   ") + classification + "\n ");
    cell(4, std::string("\n   ") + date + "\n ");
    cell(5, "n/a");
    cell(6, std::string("<span id=\"") + id + "_size\">1 MB</span> <span class=\"noDisplay\" id=\"" + id +
                "_originalSize\">" + size + "</span>");
    return s;
}

std::string searchPage() {
    std::string page = "<html><table><tr id=\"headerRow\"><th>Title</th>";
    page += row("4458688f-7272-47d0-8f33-ad5ee6337305", 0,
                "2026-09 Setup Dynamic Update for Windows 11, version 25H2 for x64-based Systems (KB5127216)",
                "Windows 10 and later Dynamic Update", "Critical Updates", "9/22/2026", "18316225");
    page += row("522b53b9-7eea-415d-9a2d-d4cbd0a37abf", 1,
                "2026-09 Cumulative Update Preview for Windows 11, version 25H2 for x64-based Systems (KB5124010) (26200.9550)",
                "Windows 11", "Updates", "9/22/2026", "5223465117");
    page += row("5cc91450-4f0f-40a8-a67a-63dc58d9dac9", 2,
                "2026-09 Cumulative Update for Windows 11, version 25H2 for x64-based Systems (KB5129195) (26200.9457)",
                "Windows 11", "Security Updates", "9/14/2026", "5173184334");
    page += row("3382e048-55a0-4ae4-a0f0-6441249beeda", 3,
                "2026-09 Cumulative Update for Windows 11, version 25H2 for x64-based Systems (KB5124008) (26200.9445)",
                "Windows 11", "Security Updates", "9/8/2026", "5174619566");
    page += row("f3bfe0d5-fae9-4257-89b5-0062ddceac06", 4,
                "2026-09 Cumulative Update for .NET Framework 3.5 and 4.8.1 for Windows 11, version 25H2 for x64 (KB5126052)",
                "Windows 11", "Security Updates", "9/8/2026", "96738688");
    page += row("a264f3d2-badd-495e-aaef-b953a6b9f9c6", 5,
                "2026-08 Cumulative Update Preview for .NET Framework 3.5 and 4.8.1 for Windows 11, version 25H2 for x64 (KB5122385)",
                "Windows 11", "Updates", "8/27/2026", "96754028");
    // Other architecture / release / Server / hotpatch: never offered.
    page += row("11111111-2222-3333-4444-555555555555", 6,
                "2026-09 Cumulative Update for Windows 11, version 25H2 for arm64-based Systems (KB5129195) (26200.9457)",
                "Windows 11", "Security Updates", "9/14/2026", "5000");
    page += row("11111111-2222-3333-4444-555555555556", 7,
                "2026-09 Cumulative Update for Windows 11, version 24H2 for x64-based Systems (KB5129195) (26100.9457)",
                "Windows 11", "Security Updates", "9/14/2026", "5000");
    page += row("11111111-2222-3333-4444-555555555557", 8,
                "2026-09 Hotpatch Cumulative Update for Windows 11, version 25H2 for x64-based Systems (KB5129999) (26200.9460)",
                "Windows 11", "Security Updates", "9/15/2026", "5000");
    page += "</tr></table></html>";
    return page;
}

const char* const kDownloadScript = R"(
var downloadInformation = new Array();
downloadInformation[0] = new Object();
downloadInformation[0].updateID ='5cc91450-4f0f-40a8-a67a-63dc58d9dac9';
downloadInformation[0].enTitle ='2026-09 Cumulative Update for Windows 11, version 25H2 for x64-based Systems (KB5129195) (26200.9457)';
downloadInformation[0].files = new Array();
downloadInformation[0].files[0] = new Object();
downloadInformation[0].files[0].url = 'https://catalog.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/471c8f54-b9fc-491d-9944-234d5db009f0/public/windows11.0-kb5129195-x64_ed361878ec2b56a7dfdb8f256565263a7fdc8eaa.msu';
downloadInformation[0].files[0].digest = '7TYYeOwrVqff248lZWUmOn/cjqo=';
downloadInformation[0].files[0].sha256 = 'sz/3qrW7u7c4Z/P1wvSS/pfAEpuWqhlBvCcGENeHbfo=';
downloadInformation[0].files[0].fileName = 'windows11.0-kb5129195-x64_ed361878ec2b56a7dfdb8f256565263a7fdc8eaa.msu';
downloadInformation[0].files[1] = new Object();
downloadInformation[0].files[1].url = 'https://catalog.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/d8b7f92b-bd35-4b4c-96e5-46ce984b31e0/public/windows11.0-kb5043080-x64_953449672073f8fb99badb4cc6d5d7849b9c83e8.msu';
downloadInformation[0].files[1].sha256 = 'gZYyYQoQGvEccpB/sT/wW1+0IiwOJj7poDvMHR1fJqQ=';
downloadInformation[0].files[1].fileName = 'windows11.0-kb5043080-x64_953449672073f8fb99badb4cc6d5d7849b9c83e8.msu';
downloadInformation[0].allFilesExist = false;
var files = downloadInformation[0].files;
)";

} // namespace

TEST_CASE("update catalog: the image build names the catalog's product") {
    auto t = catalogTarget(26200, 8037, L"x64");
    CHECK(t.windows == 11);
    CHECK(t.release == L"25H2");
    CHECK(t.revision == 8037);
    t = catalogTarget(19045, 3803, L"AMD64");
    CHECK(t.windows == 10);
    CHECK(t.release == L"22H2");
    CHECK(t.architecture == L"x64");
    CHECK(catalogTarget(27000, 1, L"x64").release.empty()); // unknown build: no search
    CHECK(catalogQueries(catalogTarget(27000, 1, L"x64")).empty());
    const auto queries = catalogQueries(catalogTarget(26100, 1, L"arm64"));
    REQUIRE(queries.size() == 2);
    CHECK(queries[0] == L"Cumulative Update for Windows 11 Version 24H2 for arm64");
}

TEST_CASE("update catalog: search page rows are parsed and classified") {
    const auto entries = parseCatalogSearch(searchPage());
    REQUIRE(entries.size() == 9);
    CHECK(entries[0].kind == CatalogKind::Setup);
    const auto& preview = entries[1];
    CHECK(preview.kind == CatalogKind::Cumulative);
    CHECK(preview.preview);
    const auto& lcu = entries[2];
    CHECK(lcu.id == L"5cc91450-4f0f-40a8-a67a-63dc58d9dac9");
    CHECK(lcu.title == L"2026-09 Cumulative Update for Windows 11, version 25H2 for x64-based Systems (KB5129195) (26200.9457)");
    CHECK(lcu.products == L"Windows 11");
    CHECK(lcu.classification == L"Security Updates");
    CHECK(lcu.kb == L"KB5129195");
    CHECK(lcu.build == 26200);
    CHECK(lcu.revision == 9457);
    CHECK(lcu.dateKey() == 20260914);
    CHECK(lcu.size == 5173184334ull);
    CHECK_FALSE(lcu.preview);
    CHECK(entries[4].kind == CatalogKind::DotNet);
    CHECK(entries[4].revision == 0);
    CHECK(parseCatalogSearch("<html>no results</html>").empty());
}

TEST_CASE("update catalog: the newest cumulative and .NET update are offered, previews only when newer") {
    const auto entries = parseCatalogSearch(searchPage());
    auto offers = pickCatalogOffers(entries, catalogTarget(26200, 8037, L"x64"));
    REQUIRE(offers.size() == 3);
    CHECK(offers[0].entry.kb == L"KB5129195");
    CHECK(offers[0].recommended);
    CHECK_FALSE(offers[0].olderThanImage);
    CHECK(offers[1].entry.kb == L"KB5124010"); // the preview after it
    CHECK_FALSE(offers[1].recommended);
    CHECK(offers[2].entry.kb == L"KB5126052"); // .NET; its August preview is older: left out
    CHECK(offers[2].recommended);

    // An image that already has 26200.9457 is told so.
    offers = pickCatalogOffers(entries, catalogTarget(26200, 9457, L"x64"));
    CHECK(offers[0].olderThanImage);
    CHECK_FALSE(offers[1].olderThanImage);

    // Wrong architecture: nothing (only the arm64 row of the page matches).
    offers = pickCatalogOffers(entries, catalogTarget(26200, 1, L"arm64"));
    REQUIRE(offers.size() == 1);
    CHECK(offers[0].entry.id == L"11111111-2222-3333-4444-555555555555");
}

TEST_CASE("update catalog: download dialog files, SHA-256 and trusted hosts") {
    const auto files = parseCatalogDownload(kDownloadScript);
    REQUIRE(files.size() == 2);
    CHECK(files[0].fileName == L"windows11.0-kb5129195-x64_ed361878ec2b56a7dfdb8f256565263a7fdc8eaa.msu");
    REQUIRE(files[0].sha256.size() == 32);
    CHECK(files[0].sha256[0] == 0xb3); // "sz/3…" = b3 3f f7 …
    CHECK(files[0].sha256[1] == 0x3f);
    CHECK(files[0].sha256[2] == 0xf7);
    CHECK(files[1].fileName.find(L"kb5043080") != std::wstring::npos); // the checkpoint

    CHECK(trustedDownloadUrl(files[0].url));
    CHECK(trustedDownloadUrl(L"http://download.windowsupdate.com/c/msdownload/update/x.msu"));
    CHECK_FALSE(trustedDownloadUrl(L"http://catalog.sf.dl.delivery.mp.microsoft.com/x.msu")); // http: only windowsupdate.com
    CHECK_FALSE(trustedDownloadUrl(L"https://evil.example/microsoft.com/x.msu"));
    CHECK_FALSE(trustedDownloadUrl(L"https://microsoft.com.evil.example/x.msu"));
    CHECK_FALSE(trustedDownloadUrl(L"https://evilmicrosoft.com/x.msu"));
    CHECK_FALSE(trustedDownloadUrl(L"file:///C:/x.msu"));
    // "user:pass@" before the real host: WinHTTP would connect to evil.example.
    CHECK_FALSE(trustedDownloadUrl(L"https://download.microsoft.com:x@evil.example/f.msu"));
    CHECK_FALSE(trustedDownloadUrl(L"https://download.microsoft.com@evil.example/f.msu"));
    CHECK(trustedDownloadUrl(L"https://download.microsoft.com:443/f.msu"));

    CHECK(urlEncode(R"([{"a":"b c"}])") == "%5B%7B%22a%22%3A%22b%20c%22%7D%5D");
}
