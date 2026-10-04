// D-061: language files as Windows Update (uupdump.net) names them, what DISM needs them to be
// called, and the uupdump.net API answers (trimmed copies of what it returned on 2026-10-04 for
// 26200.8037).
#include "core/image/LanguagePacks.h"
#include "core/ops/ChangeSet.h"
#include "core/ops/Planner.h"
#include "core/updates/UupLanguages.h"

#include <doctest.h>

#include <string>

using namespace wl;
using namespace wl::core;
using K = LanguagePackFile::Kind;

TEST_CASE("language files: Windows Update (UUP) names") {
    const auto lp = classifyLanguageFile(L"C:\\x\\Microsoft-Windows-Client-LanguagePack-Package-amd64-en-us.esd");
    CHECK(lp.kind == K::LanguagePack);
    CHECK(lp.language == L"en-US");
    CHECK(lp.architecture == L"x64");
    CHECK(cbsFileName(lp).empty()); // the pack has no capability: DISM takes it as it is
    const auto tr = classifyLanguageFile(L"Microsoft-Windows-Client-LanguagePack-Package-amd64-sr-Latn-RS.esd");
    CHECK(tr.language == L"sr-Latn-RS");
    CHECK(classifyLanguageFile(L"Microsoft-Windows-Client-LanguagePack-Package_tr-tr-arm64-tr-tr.esd").architecture == L"arm64");

    const auto basic = classifyLanguageFile(L"Microsoft-Windows-LanguageFeatures-Basic-en-us-Package-amd64.cab");
    CHECK(basic.kind == K::Basic);
    CHECK(basic.language == L"en-US");
    CHECK(basic.packageArch == L"amd64");
    CHECK(cbsFileName(basic) == L"Microsoft-Windows-LanguageFeatures-Basic-en-us-Package~31bf3856ad364e35~amd64~~.cab");
    const auto fonts = classifyLanguageFile(L"Microsoft-Windows-LanguageFeatures-Fonts-Jpan-Package-amd64.cab");
    CHECK(fonts.kind == K::Fonts);
    CHECK(fonts.component == L"Jpan");
    CHECK(fonts.language.empty());

    const auto wow = classifyLanguageFile(L"Microsoft-Windows-MediaPlayer-Package-wow64-tr-TR.cab");
    CHECK(wow.kind == K::Satellite);
    CHECK(wow.component == L"Microsoft-Windows-MediaPlayer-Package");
    CHECK(wow.packageArch == L"wow64");
    CHECK(wow.architecture == L"x64"); // a 32-bit part of an x64 image
    CHECK(wow.language == L"tr-TR");
    CHECK(cbsFileName(wow) == L"Microsoft-Windows-MediaPlayer-Package~31bf3856ad364e35~wow64~tr-TR~.cab");
    CHECK(classifyLanguageFile(L"HyperV-OptionalFeature-VirtualMachinePlatform-Client-Disabled-FOD-Package-amd64-en-us.cab").kind ==
          K::Satellite);

    // Express (PSF) metadata next to every cab: no package, never offered.
    for (const wchar_t* name : {L"Microsoft-Windows-LanguageFeatures-Basic-en-us-Package-amd64_80a67e0b.cab",
                                L"Microsoft-Windows-LanguageFeatures-Basic-en-us-Package.cab",
                                L"Microsoft-Windows-MediaPlayer-Package-amd64-tr-TR_a029d1bf.cab"}) {
        CAPTURE(std::wstring(name));
        CHECK(isExpressMetadata(name));
        CHECK_FALSE(isLanguageFile(name));
    }
    // Neither language files nor express metadata.
    CHECK_FALSE(isExpressMetadata(L"Microsoft-Windows-LanguageFeatures-Basic-en-us-Package~31bf3856ad364e35~amd64~~.cab"));
    CHECK_FALSE(isLanguageFile(L"professional_en-us.esd"));
    CHECK_FALSE(isLanguageFile(L"Microsoft-Windows-MediaPlayer-Package-amd64.cab"));
    CHECK_FALSE(isLanguageFile(L"Microsoft-Windows-MediaPlayer-Package~31bf3856ad364e35~amd64~~.cab")); // the component itself
}

TEST_CASE("language files: LoF names stay as they are") {
    const auto lof = classifyLanguageFile(L"D:\\Microsoft-Windows-LanguageFeatures-OCR-tr-tr-Package~31bf3856ad364e35~amd64~~.cab");
    CHECK(lof.kind == K::Ocr);
    CHECK(cbsFileName(lof) == lof.path.filename().wstring());
    const auto sat = classifyLanguageFile(L"D:\\Microsoft-Windows-Notepad-System-FoD-Package~31bf3856ad364e35~wow64~tr-TR~.cab");
    CHECK(sat.kind == K::Satellite);
    CHECK(sat.language == L"tr-TR");
    CHECK(sat.component == L"Microsoft-Windows-Notepad-System-FoD-Package");
    CHECK(cbsFileName(sat) == sat.path.filename().wstring());
}

TEST_CASE("language files: dependencies, fonts and the components an image has") {
    CHECK(featureDependencies(K::Speech) == std::vector<K>{K::Basic, K::TextToSpeech});
    CHECK(featureDependencies(K::Ocr) == std::vector<K>{K::Basic});
    CHECK(featureDependencies(K::Basic).empty());
    CHECK(requiredFontScripts(L"ja-JP") == std::vector<std::wstring>{L"Jpan"});
    CHECK(requiredFontScripts(L"zh-TW") == std::vector<std::wstring>{L"Hant"});
    CHECK(requiredFontScripts(L"pa-IN") == std::vector<std::wstring>{L"Guru"});
    CHECK(requiredFontScripts(L"pa-Arab-PK") == std::vector<std::wstring>{L"Arab"}); // the longer prefix wins
    CHECK(requiredFontScripts(L"en-US").empty());
    CHECK(requiredFontScripts(L"tr-TR").empty());

    const std::vector<std::wstring> image{
        L"Microsoft-Windows-MediaPlayer-Package~31bf3856ad364e35~wow64~~10.0.26100.8036",
        L"Microsoft-Windows-MediaPlayer-Package~31bf3856ad364e35~wow64~tr-TR~10.0.26100.1",
        L"Microsoft-Windows-WMIC-FoD-Package~31bf3856ad364e35~amd64~tr-TR~10.0.26100.1"}; // language part only
    CHECK(satelliteFits(classifyLanguageFile(L"Microsoft-Windows-MediaPlayer-Package-wow64-en-us.cab"), image));
    CHECK_FALSE(satelliteFits(classifyLanguageFile(L"Microsoft-Windows-MediaPlayer-Package-amd64-en-us.cab"), image));
    CHECK_FALSE(satelliteFits(classifyLanguageFile(L"Microsoft-Windows-WMIC-FoD-Package-amd64-en-us.cab"), image));
}

TEST_CASE("language files: install order in the plan") {
    ops::ChangeSet changes;
    changes.add({ops::OpKind::AddPackage, L"C:\\l\\Microsoft-Windows-WMIC-FoD-Package-amd64-en-us.cab", L"language"});
    changes.add({ops::OpKind::AddPackage, L"C:\\l\\Microsoft-Windows-LanguageFeatures-Speech-en-us-Package-amd64.cab", L"language"});
    changes.add({ops::OpKind::AddPackage, L"C:\\u\\windows11.0-kb5129195-x64.msu", L"lcu"});
    changes.add({ops::OpKind::AddPackage, L"C:\\l\\Microsoft-Windows-LanguageFeatures-TextToSpeech-en-us-Package-amd64.cab", L"language"});
    changes.add({ops::OpKind::AddPackage, L"C:\\l\\Microsoft-Windows-LanguageFeatures-Basic-en-us-Package-amd64.cab", L"language"});
    changes.add({ops::OpKind::AddPackage, L"C:\\l\\Microsoft-Windows-Client-LanguagePack-Package-amd64-en-us.esd", L"language"});
    const auto plan = ops::plan(changes);
    REQUIRE(plan.steps.size() == 6);
    CHECK(plan.steps[0].operation.target.ends_with(L"amd64-en-us.esd"));
    CHECK(plan.steps[1].operation.target.find(L"Basic") != std::wstring::npos);
    CHECK(plan.steps[2].operation.target.find(L"TextToSpeech") != std::wstring::npos);
    CHECK(plan.steps[3].operation.target.find(L"Speech-") != std::wstring::npos);
    CHECK(plan.steps[4].operation.target.find(L"WMIC") != std::wstring::npos);
    CHECK(plan.steps[5].operation.value == L"lcu"); // Microsoft's order: languages before the cumulative update
}

namespace {

constexpr std::string_view kBuilds = R"j({"response":{"apiVersion":"x","builds":{
 "0":{"title":"Windows 11, version 25H2 (26200.8037)","build":"26200.8037","arch":"arm64","created":1773162065,"uuid":"be600926-784e-4e49-a9e1-4496eadf16ca"},
 "1":{"title":"Windows 11, version 25H2 (26200.8037)","build":"26200.8037","arch":"amd64","created":1773162043,"uuid":"db640db1-d7b0-4d44-8bbc-b930ab72865a"},
 "2":{"title":"Windows 11 Insider Preview 26200.9001 (ge_release)","build":"26200.9001","arch":"amd64","created":1780000000,"uuid":"11111111-2222-3333-4444-555555555555"},
 "3":{"title":"Windows 11, version 25H2 (26200.7920)","build":"26200.7920","arch":"amd64","created":1770000000,"uuid":"aaaaaaaa-2222-3333-4444-555555555555"}}}})j";

std::string file(const char* name, int size, const char* sha256) {
    return std::string("\"") + name + R"(":{"sha1":"x","sha256":")" + sha256 + R"(","size":")" + std::to_string(size) +
           R"(","url":"http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/1?P1=2","uuid":null,"expire":0,"debug":null})";
}

std::string filesJson() {
    const char* h = "aa";
    std::string j = R"j({"response":{"apiVersion":"x","updateName":"Windows 11, version 25H2 (26200.8037)","arch":"amd64","build":"26200.8037","files":{)j";
    const char* names[] = {"Microsoft-Windows-Client-LanguagePack-Package-amd64-en-us.esd",
                           "Microsoft-Windows-LanguageFeatures-Basic-en-us-Package-amd64.cab",
                           "Microsoft-Windows-LanguageFeatures-Basic-en-us-Package-amd64_80a67e0b.cab",
                           "Microsoft-Windows-LanguageFeatures-Basic-en-us-Package.cab",
                           "Microsoft-Windows-LanguageFeatures-Speech-en-us-Package-amd64.cab",
                           "Microsoft-Windows-MediaPlayer-Package-wow64-en-us.cab",
                           "Microsoft-Windows-Client-LanguagePack-Package-amd64-ja-JP.esd",
                           "Microsoft-Windows-LanguageFeatures-Basic-ja-jp-Package-amd64.cab",
                           "Microsoft-Windows-LanguageFeatures-Fonts-Jpan-Package-amd64.cab",
                           "Microsoft-Windows-LanguageFeatures-Fonts-Thai-Package-amd64.cab",
                           "Microsoft-Windows-LanguageFeatures-Basic-af-za-Package-amd64.cab", // no pack: not offered
                           "LanguageExperiencePack.af-za.Neutral.appx",
                           "professional_en-us.esd",
                           "Microsoft-Windows-Client-LanguagePack-Package-arm64-de-DE.esd"}; // other architecture
    bool first = true;
    int size = 1000;
    for (const char* n : names) {
        j += (first ? "" : ",") + file(n, size++, h);
        first = false;
    }
    return j + "}}}";
}

} // namespace

TEST_CASE("uupdump.net: the build for an image") {
    const auto builds = parseUupBuilds(kBuilds);
    REQUIRE(builds);
    REQUIRE(builds->size() == 4);
    CHECK((*builds)[1].revision == 8037);
    CHECK(pickUupBuild(*builds, 26200, 8037, L"x64")->uuid == L"db640db1-d7b0-4d44-8bbc-b930ab72865a");
    CHECK(pickUupBuild(*builds, 26200, 8037, L"arm64")->uuid == L"be600926-784e-4e49-a9e1-4496eadf16ca");
    // Not listed: the newest release of the build, never an Insider one.
    CHECK(pickUupBuild(*builds, 26200, 8100, L"x64")->revision == 8037);
    CHECK_FALSE(pickUupBuild(*builds, 26100, 1, L"x64"));
    CHECK_FALSE(pickUupBuild(*builds, 26200, 8037, L"x86"));
    CHECK_FALSE(parseUupBuilds(R"({"response":{"error":"NO_SEARCH_RESULTS"}})"));
    CHECK_FALSE(parseUupBuilds("<html>"));
}

TEST_CASE("uupdump.net: the languages of a build") {
    const auto files = parseUupFiles(filesJson());
    REQUIRE(files);
    CHECK(files->size() == 14);
    const auto languages = uupLanguages(*files, L"x64");
    REQUIRE(languages.size() == 2); // en-US, ja-JP: languages with a pack for x64
    const auto& en = languages[0];
    CHECK(en.language == L"en-US");
    REQUIRE(en.files.size() == 4); // pack, Basic, Speech, MediaPlayer (express metadata left out)
    CHECK(en.files[0].file.kind == K::LanguagePack);
    CHECK(en.files[1].file.kind == K::Basic);
    CHECK(en.files[2].file.kind == K::Speech);
    CHECK(en.files[3].file.kind == K::Satellite);
    CHECK(en.find(K::Basic)->source.size == 1001);
    CHECK(uupSaveName(en.files[0]) == L"Microsoft-Windows-Client-LanguagePack-Package-amd64-en-us.esd");
    CHECK(uupSaveName(en.files[1]) == L"Microsoft-Windows-LanguageFeatures-Basic-en-us-Package~31bf3856ad364e35~amd64~~.cab");
    CHECK(uupSaveName(en.files[3]) == L"Microsoft-Windows-MediaPlayer-Package~31bf3856ad364e35~wow64~en-US~.cab");
    const auto& ja = languages[1];
    CHECK(ja.language == L"ja-JP");
    REQUIRE(ja.find(K::Fonts));
    CHECK(ja.find(K::Fonts)->file.component == L"Jpan"); // its script's fonts, not Thai
    CHECK(ja.files.size() == 3);
    CHECK(uupLanguages(*files, L"arm64").size() == 1);
    CHECK_FALSE(parseUupFiles(R"({"response":{"error":"UNSUPPORTED_COMBINATION"}})"));
}

TEST_CASE("uupdump.net: only Microsoft's servers are downloaded from") {
    CHECK(trustedUupUrl(L"http://tlu.dl.delivery.mp.microsoft.com/filestreamingservice/files/de43?P1=1"));
    CHECK(trustedUupUrl(L"https://dl.delivery.mp.microsoft.com/x"));
    CHECK(trustedUupUrl(L"http://download.windowsupdate.com/c/msdownload/x.cab"));
    CHECK_FALSE(trustedUupUrl(L"http://tlu.dl.delivery.mp.microsoft.com.evil.example/x"));
    CHECK_FALSE(trustedUupUrl(L"http://microsoft.com@evil.example/x"));
    CHECK_FALSE(trustedUupUrl(L"http://evilmicrosoft.com/x"));
    CHECK_FALSE(trustedUupUrl(L"ftp://tlu.dl.delivery.mp.microsoft.com/x"));
    CHECK_FALSE(trustedUupUrl(L"https://uupdump.net/getfile.php?id=1"));
}
