// D-093: UUP dump API answers → builds, languages, editions, files. Trimmed copies of what
// api.uupdump.net returned on 2026-10-09 (Windows 11 26H2, 26300.9550 amd64).
#include "core/uup/UupCatalog.h"

#include <doctest.h>

using namespace wl;
using namespace wl::core::uup;

TEST_CASE("UUP: builds are classified by title and come newest first") {
    const char* text = R"J({"response":{"apiVersion":"x","builds":{
        "45":{"title":".NET Framework Security Update for Windows 11 - KB5126054 (26300.10006)","build":"26300.10006","arch":"amd64","created":1790436920,"uuid":"de359397-a5f7-45ce-80d1-47fbdb12a3a5"},
        "68":{"title":"Windows 11, version 26H2 (26300.9550)","build":"26300.9550","arch":"amd64","created":1790096444,"uuid":"fe47c57f-a77f-41e3-9012-00b4187e3462"},
        "70":{"title":"Windows 11 Insider Preview 27965.1000 (rs_prerelease)","build":"27965.1000","arch":"arm64","created":1790500000,"uuid":"11111111-2222-3333-4444-555555555555"},
        "71":{"title":"Preview Update for Windows 11 (26300.9278)","build":"26300.9278","arch":"amd64","created":1789479943,"uuid":"48dc4928-11dd-4f6a-9e75-4070c89054b0"},
        "72":{"title":"Windows Server 2025 (26100.6899)","build":"26100.6899","arch":"amd64","created":1700000000,"uuid":"aaaaaaaa-2222-3333-4444-555555555555"}
    }},"jsonApiVersion":"0.2.3"})J";
    const auto builds = parseBuilds(text);
    REQUIRE(builds);
    REQUIRE(builds->size() == 5);
    CHECK((*builds)[0].build == L"27965.1000");
    CHECK((*builds)[0].kind == BuildKind::Insider);
    CHECK((*builds)[0].arch == L"arm64");
    CHECK((*builds)[1].kind == BuildKind::Update); // .NET
    CHECK((*builds)[2].kind == BuildKind::Release);
    CHECK((*builds)[2].id == L"fe47c57f-a77f-41e3-9012-00b4187e3462");
    CHECK((*builds)[2].created == 1790096444);
    CHECK((*builds)[3].kind == BuildKind::Update); // a preview cumulative update alone
    CHECK((*builds)[4].kind == BuildKind::Server);
    CHECK(buildKind(L"Windows 10, version 22H2 (19045.6332)") == BuildKind::Release);
    CHECK(buildKind(L"Cumulative Update for Windows 11 Insider Preview (27965.1010)") == BuildKind::Update);
}

TEST_CASE("UUP: languages with their names, editions with theirs") {
    const auto langs = parseLanguages(
        R"J({"response":{"langList":["tr-tr","en-us"],"langFancyNames":{"tr-tr":"Turkish","en-us":"English (United States)"}}})J");
    REQUIRE(langs);
    REQUIRE(langs->size() == 2);
    CHECK((*langs)[0].code == L"en-us"); // sorted by name
    CHECK((*langs)[1].name == L"Turkish");
    const auto eds = parseEditions(
        R"J({"response":{"editionList":["PROFESSIONAL","CORE"],"editionFancyNames":{"PROFESSIONAL":"Windows Pro","CORE":"Windows Home"}}})J");
    REQUIRE(eds);
    REQUIRE(eds->size() == 2);
    CHECK((*eds)[0].code == L"PROFESSIONAL");
    CHECK((*eds)[1].name == L"Windows Home");
}

TEST_CASE("UUP: files with sizes, hashes, links and their kind") {
    const char* text = R"J({"response":{"updateName":"Windows 11, version 26H2 (26300.9550)","arch":"amd64","build":"26300.9550","sku":48,"hasUpdates":true,"appxPresent":true,"files":{
        "professional_tr-tr.esd":{"sha1":"909d5d62ac","sha256":"EAD118EBFE","size":"554439417","url":"http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/x?P1=1","uuid":"u"},
        "Microsoft-Windows-Client-Desktop-Required-Package.ESD":{"sha1":"74cd","sha256":"670b","size":"814881824","url":"","uuid":"u"},
        "Windows11.0-KB5124010-x64.msu":{"sha1":"95a6","sha256":"2bb0","size":"4689700000","url":null,"uuid":"u"},
        "Microsoft-Windows-Hello-Face-Package-amd64.cab":{"sha1":"88aa","sha256":"5a83","size":"60000000","url":"","uuid":"u"},
        "Edge.wim":{"sha1":"1f0b","sha256":"799e","size":"180196966","url":"","uuid":"u"},
        "AppInstaller_x64.msix":{"sha1":"aa","sha256":"bb","size":"59200000","url":"","uuid":"u"}
    }}})J";
    const auto set = parseFiles(text);
    REQUIRE(set);
    CHECK(set->updateName == L"Windows 11, version 26H2 (26300.9550)");
    CHECK(set->hasUpdates);
    CHECK(set->appxPresent);
    REQUIRE(set->files.size() == 6);
    CHECK(set->totalSize() == 554439417ull + 814881824ull + 4689700000ull + 60000000ull + 180196966ull + 59200000ull);
    auto find = [&](const wchar_t* name) {
        for (const auto& f : set->files) {
            if (f.name == name) {
                return f;
            }
        }
        FAIL("missing");
        return File{};
    };
    const File meta = find(L"professional_tr-tr.esd");
    CHECK(meta.kind == FileKind::Metadata);
    CHECK(meta.sha256 == L"ead118ebfe"); // lower-case like sha256File
    CHECK(meta.url.starts_with(L"http://tlu.dl.delivery.mp.microsoft.com/"));
    CHECK(find(L"Microsoft-Windows-Client-Desktop-Required-Package.ESD").kind == FileKind::PackageEsd);
    CHECK(find(L"Windows11.0-KB5124010-x64.msu").kind == FileKind::Msu);
    CHECK(find(L"Windows11.0-KB5124010-x64.msu").url.empty()); // null → none
    CHECK(find(L"Microsoft-Windows-Hello-Face-Package-amd64.cab").kind == FileKind::Cab);
    CHECK(find(L"Edge.wim").kind == FileKind::Edge);
    CHECK(find(L"AppInstaller_x64.msix").kind == FileKind::App);
    CHECK(fileKind(L"core_en-us.esd") == FileKind::Metadata);
    CHECK(fileKind(L"professionaln_sr-latn-rs.esd") == FileKind::Metadata);
    CHECK(fileKind(L"Microsoft-Windows-Client-LanguagePack-Package-amd64-tr-TR.esd") == FileKind::PackageEsd);
}

TEST_CASE("UUP: an error answer is an error") {
    const auto limited = parseFiles(R"J({"response":{"error":"USER_RATE_LIMITED"},"jsonApiVersion":"0.2.3"})J");
    REQUIRE_FALSE(limited);
    CHECK(limited.error().message == L"USER_RATE_LIMITED");
    const auto lang = parseFiles(R"J({"response":{"error":"UNSUPPORTED_LANG"}})J");
    REQUIRE_FALSE(lang);
    CHECK(lang.error().code == ErrorCode::NotFound);
    CHECK_FALSE(parseBuilds("<html>"));
}
