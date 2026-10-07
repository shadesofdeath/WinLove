// D-078: winget's repository read without winget — the YAML it writes, its version lists and
// merged manifests, MSZIP, the signed index (a small SQLite file of the same shape here), and the
// programs the post-setup plan installs at the first logon.
#include "core/postsetup/PostSetup.h"
#include "core/programs/Winget.h"
#include "core/programs/Yaml.h"

#include <doctest.h>

#include <windows.h>

#include <compressapi.h>
#include <winsqlite/winsqlite3.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::core;

namespace {

std::string field(const nlohmann::json& node, const char* key) {
    return node.at(key).get<std::string>();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"winget";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string readAll(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("winget YAML: mappings, sequences, quoted and block scalars as winget writes them") {
    // The shape of a merged manifest (VideoLAN.VLC 3.0.24, shortened): a double-quoted description
    // over several lines, a sequence at its key's column, a sequence of mappings, escapes.
    const std::string yaml = "PackageIdentifier: VideoLAN.VLC\r\n"
                             "# a comment\r\n"
                             "Commands:\r\n"
                             "- vlc\r\n"
                             "Description: \"VLC is a free and open source cross-platform multimedia player and framework\\nthat\r\n"
                             "  plays most multimedia files as well as DVDs.\\n\\nSimple, fast and powerful\r\n"
                             "  Plays everything.\"\r\n"
                             "Copyright: \"Copyright \\xA9 1999-2026 Igor Pavlov.\"\r\n"
                             "ProductCode: '{23170F69-40C1-2702-2604-000001000000}'\r\n"
                             "Quote: 'it''s'\r\n"
                             "Chinese: \"\\u5E38\\u89C1\"\r\n"
                             "ReleaseNotes: |-\r\n"
                             "  - Fixed one.\r\n"
                             "  - Fixed two.\r\n"
                             "Folded: >-\r\n"
                             "  one\r\n"
                             "  two\r\n"
                             "\r\n"
                             "  three\r\n"
                             "Plain: a value\r\n"
                             "  that goes on\r\n"
                             "Empty: []\r\n"
                             "Nothing:\r\n"
                             "Installers:\r\n"
                             "- Architecture: x64\r\n"
                             "  InstallerType: wix\r\n"
                             "  InstallerSwitches:\r\n"
                             "    Silent: /quiet /norestart\r\n"
                             "  ExpectedReturnCodes:\r\n"
                             "  - InstallerReturnCode: 1641\r\n"
                             "    ReturnResponse: rebootInitiated\r\n"
                             "- Architecture: arm64\r\n"
                             "ManifestType: merged\r\n";
    const auto doc = parseYaml(yaml);
    REQUIRE(doc.has_value());
    CHECK(field(*doc, "PackageIdentifier") == "VideoLAN.VLC");
    CHECK((*doc)["Commands"] == nlohmann::json::array({"vlc"}));
    CHECK(field(*doc, "Description") ==
          "VLC is a free and open source cross-platform multimedia player and framework\nthat plays most multimedia files as "
          "well as DVDs.\n\nSimple, fast and powerful Plays everything.");
    CHECK(field(*doc, "Copyright") == "Copyright \xC2\xA9 1999-2026 Igor Pavlov.");
    CHECK(field(*doc, "ProductCode") == "{23170F69-40C1-2702-2604-000001000000}");
    CHECK(field(*doc, "Quote") == "it's");
    CHECK(field(*doc, "Chinese") == "\xE5\xB8\xB8\xE8\xA7\x81");
    CHECK(field(*doc, "ReleaseNotes") == "- Fixed one.\n- Fixed two.");
    CHECK(field(*doc, "Folded") == "one two\nthree");
    CHECK(field(*doc, "Plain") == "a value that goes on");
    CHECK((*doc)["Empty"].is_array());
    CHECK((*doc)["Nothing"].is_null());
    const auto& installers = (*doc)["Installers"];
    REQUIRE(installers.size() == 2);
    CHECK(field(installers[0], "InstallerType") == "wix");
    CHECK(field(installers[0]["InstallerSwitches"], "Silent") == "/quiet /norestart");
    CHECK(field(installers[0]["ExpectedReturnCodes"][0], "ReturnResponse") == "rebootInitiated");
    CHECK(field(installers[1], "Architecture") == "arm64");
    CHECK(field(*doc, "ManifestType") == "merged");

    // What it does not read is refused, not guessed at.
    CHECK_FALSE(parseYaml("Key: &anchor value\n").has_value());
    CHECK_FALSE(parseYaml("Key: [a, b]\n").has_value());
    CHECK_FALSE(parseYaml("Key:\n\tSub: x\n").has_value());
    CHECK_FALSE(parseYaml("Key: \"no end\n").has_value());
    CHECK(field(parseYaml("Key: value\n   more: x\n").value(), "Key") == "value more: x"); // a plain scalar goes on
    CHECK(field(parseYaml("Key: |\n  a\n\n  b\n").value(), "Key") == "a\n\nb\n");            // literal, clipped
    CHECK(parseYaml("").value().empty());
}

TEST_CASE("winget version list and merged manifest: latest version, localized texts, icon, installers") {
    const auto versions = parseVersionData("sV: 1.0\nvD:\n- v: 3.0.24\n  rP: manifests/v/VideoLAN/VLC/3.0.24/0fdc\n"
                                           "  s256H: 714F198D97F1FCFC144E3F6451466C14C8AFF7A558415B442BA06BFB77BFB6F5\n"
                                           "- v: 3.0.17.4\n  aMiV: 3.0.17.0\n  rP: manifests/v/VideoLAN/VLC/3.0.17.4/2035\n"
                                           "  s256H: 593c79650fffb23005a1bf93885d0db831ceeaf4be76c6620e8fee43f51168fc\n"
                                           "- v: 0.1\n  rP: ../../etc/passwd\n  s256H: 00\n");
    REQUIRE(versions.has_value());
    REQUIRE(versions->size() == 2); // a path leaving the cache is dropped
    CHECK(versions->at(0).version == L"3.0.24");
    CHECK(versions->at(0).relativePath == L"manifests/v/VideoLAN/VLC/3.0.24/0fdc");
    CHECK(versions->at(0).sha256 == L"714f198d97f1fcfc144e3f6451466c14c8aff7a558415b442ba06bfb77bfb6f5");

    const std::string manifest = "PackageIdentifier: 7zip.7zip\nPackageVersion: 26.04\nPackageName: 7-Zip\nPublisher: Igor Pavlov\n"
                                 "ShortDescription: A file archiver with a high compression ratio.\nLicense: LGPL-2.1\n"
                                 "PackageUrl: https://7-zip.org/download.html\nTags:\n- archive\n- zip\n"
                                 "Icons:\n- IconUrl: https://cdn.winget.microsoft.com/icons/a.png\n  IconFileType: png\n  IconTheme: dark\n"
                                 "- IconUrl: https://cdn.winget.microsoft.com/icons/b.ico\n  IconFileType: ico\n"
                                 "  IconSha256: 649C3C8780A116E625EE8124338642C2088B5153B4280BDF7A42B544FECF0730\n"
                                 "Localization:\n- PackageLocale: zh-CN\n  ShortDescription: zh\n"
                                 "- PackageLocale: tr-TR\n  ShortDescription: \"Y\\u00FCksek s\\u0131k\\u0131\\u015Ft\\u0131rmal\\u0131 ar\\u015Fivleyici.\"\n"
                                 "  Tags:\n  - \"ar\\u015Fiv\"\n" // escapes only in double quotes
                                 "InstallerType: wix\nScope: machine\nInstallers:\n- Architecture: x64\n- Architecture: arm64\n"
                                 "  InstallerType: exe\n  Scope: user\nManifestType: merged\n";
    const auto english = parseWingetManifest(manifest, L"en-US");
    REQUIRE(english.has_value());
    CHECK(english->name == L"7-Zip");
    CHECK(english->publisher == L"Igor Pavlov");
    CHECK(english->shortDescription == L"A file archiver with a high compression ratio.");
    CHECK(english->homepage == L"https://7-zip.org/download.html");
    CHECK(english->tags == std::vector<std::wstring>{L"archive", L"zip"});
    CHECK(english->iconUrl == L"https://cdn.winget.microsoft.com/icons/b.ico"); // the default theme, .ico
    CHECK(english->iconSha256 == L"649c3c8780a116e625ee8124338642c2088b5153b4280bdf7a42b544fecf0730");
    CHECK(english->installerTypes == std::vector<std::wstring>{L"wix", L"exe"});
    CHECK(english->scopes == std::vector<std::wstring>{L"machine", L"user"});
    CHECK(english->architectures == std::vector<std::wstring>{L"x64", L"arm64"});
    const auto turkish = parseWingetManifest(manifest, L"tr-TR");
    REQUIRE(turkish.has_value());
    CHECK(turkish->shortDescription == L"Yüksek sıkıştırmalı arşivleyici.");
    CHECK(turkish->tags == std::vector<std::wstring>{L"arşiv"});
    CHECK(parseWingetManifest(manifest, L"tr").value().shortDescription == turkish->shortDescription); // by language
}

TEST_CASE("winget helpers: MSZIP as Windows' Compression API writes it, ids, SHA-256") {
    // Compress with the same API winget's server side uses (buffer mode, MSZIP), then read it back.
    std::string text;
    for (int i = 0; i < 200; ++i) {
        text += "- v: 1.0." + std::to_string(i) + "\n  rP: manifests/a/b/" + std::to_string(i) + "\n";
    }
    COMPRESSOR_HANDLE compressor = nullptr;
    REQUIRE(CreateCompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &compressor));
    SIZE_T size = 0;
    Compress(compressor, text.data(), text.size(), nullptr, 0, &size);
    std::vector<std::byte> packed(size);
    REQUIRE(Compress(compressor, text.data(), text.size(), packed.data(), packed.size(), &size));
    CloseCompressor(compressor);
    packed.resize(size);
    CHECK(packed.size() < text.size());
    const auto unpacked = decompressMszip(packed);
    REQUIRE(unpacked.has_value());
    CHECK(*unpacked == text);
    std::vector<std::byte> junk(64, std::byte{0x41});
    CHECK_FALSE(decompressMszip(junk).has_value());

    CHECK(validWingetId(L"Notepad++.Notepad++"));
    CHECK(validWingetId(L"Python.Python.3.13"));
    CHECK(validWingetId(L"9NBLGGH4NNS1"));
    CHECK_FALSE(validWingetId(L""));
    CHECK_FALSE(validWingetId(L"a b"));
    CHECK_FALSE(validWingetId(L"x&calc"));
    CHECK_FALSE(validWingetId(L"çay"));

    const std::string abc = "abc";
    CHECK(sha256Hex({reinterpret_cast<const std::byte*>(abc.data()), abc.size()}) ==
          L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("winget index: search ranks the id, then names, then tags; hashes and metadata") {
    // A small index of the same schema as Public\index.db in source2.msix.
    const auto file = scratch(L"index.db");
    std::filesystem::remove(file);
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    const char* schema =
        "CREATE TABLE metadata([name] TEXT PRIMARY KEY NOT NULL, [value] TEXT NOT NULL) WITHOUT ROWID;"
        "CREATE TABLE packages(rowid INTEGER PRIMARY KEY, [id] TEXT NOT NULL, [name] TEXT NOT NULL, [moniker] TEXT,"
        " [latest_version] TEXT NOT NULL, [arp_min_version] TEXT, [arp_max_version] TEXT, [hash] BLOB);"
        "CREATE TABLE tags2(rowid INTEGER PRIMARY KEY, [tag] TEXT NOT NULL);"
        "CREATE TABLE tags2_map([tag] INT64 NOT NULL, [package] INT64 NOT NULL, PRIMARY KEY([tag], [package])) WITHOUT ROWID;"
        "INSERT INTO metadata VALUES('lastwritetime', '1791367162');"
        "INSERT INTO packages VALUES(1, 'Google.Chrome', 'Google Chrome', 'chrome', '155.0.8059.40', NULL, NULL,"
        " x'8003EC63FCF6F39463EDB5FBE26BBF1F8A279E1C018C14EF5958E77F6487C447');"
        "INSERT INTO packages VALUES(2, 'Chromium.ChromeDriver', 'ChromeDriver', NULL, '155.0', NULL, NULL, NULL);"
        "INSERT INTO packages VALUES(3, 'Google.Chrome.Dev', 'Google Chrome Dev', NULL, '157.0', NULL, NULL, NULL);"
        "INSERT INTO packages VALUES(4, 'Brave.Brave', 'Brave', 'brave', '1.80', NULL, NULL, NULL);"
        "INSERT INTO packages VALUES(5, 'Odd.Percent', 'Fifty%Off', NULL, '1', NULL, NULL, NULL);"
        "INSERT INTO tags2 VALUES(1, 'browser');"
        "INSERT INTO tags2_map VALUES(1, 4);";
    REQUIRE(sqlite3_exec(db, schema, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    auto index = WingetIndex::open(file);
    REQUIRE(index.has_value());
    CHECK(index->count() == 5);
    const auto chrome = index->search(L"Chrome", 10);
    REQUIRE(chrome.size() == 3);
    CHECK(chrome[0].id == L"Google.Chrome");      // its moniker
    CHECK(chrome[1].id == L"Chromium.ChromeDriver"); // a name that starts with it
    CHECK(chrome[2].id == L"Google.Chrome.Dev");  // a name that holds it
    CHECK(index->search(L"chrome", 1).size() == 1);
    CHECK(index->search(L"browser", 10).front().id == L"Brave.Brave"); // by tag
    CHECK(index->search(L"%", 10).size() == 1);   // LIKE wildcards are plain text
    CHECK(index->search(L"  ", 10).empty());
    CHECK(index->find(L"google.chrome").value().version == L"155.0.8059.40");
    CHECK_FALSE(index->find(L"Google").has_value());
    CHECK(index->tags(L"Brave.Brave") == std::vector<std::wstring>{L"browser"});
    CHECK(index->versionDataHash(L"Google.Chrome") == L"8003ec63fcf6f39463edb5fbe26bbf1f8a279e1c018c14ef5958e77f6487c447");
    CHECK(index->versionDataHash(L"Brave.Brave").empty());
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(index->builtAt().time_since_epoch()).count() == 1791367162);

    CHECK_FALSE(WingetIndex::open(scratch(L"missing.db")).has_value());
}

TEST_CASE("post-setup programs: in the plan, their own task, the window's JSON, written into the image") {
    PostSetupPlan plan;
    plan.programs = {{L"7zip.7zip", L"7-Zip"}, {L"VideoLAN.VLC", L"VLC media player"}};
    plan.programTexts = {{L"heading", L"Programlar kuruluyor"}};
    CHECK_FALSE(plan.empty());

    const auto back = postSetupFromJson(postSetupToJson(plan));
    REQUIRE(back.has_value());
    CHECK(back->programs == plan.programs);
    CHECK(back->programTexts == plan.programTexts);
    CHECK(postSetupFromJson(R"({"steps":[]})").value().programs.empty()); // plans from before D-078

    // Programs only: the machine script registers the programs task, nothing runs as the user script.
    const auto scripts = buildPostSetupScripts(plan);
    CHECK(scripts.machine.find(L"schtasks /create /tn \"WinLove Programs\" /xml \"%WL%\\programs-task.xml\"") != std::wstring::npos);
    CHECK(scripts.machine.find(L"WinLove Post-Setup") == std::wstring::npos);
    CHECK(scripts.user.empty());
    const std::wstring task = programsTaskXml();
    CHECK(task.find(L"<GroupId>S-1-5-32-545</GroupId>") != std::wstring::npos);
    CHECK(task.find(L"<RunLevel>HighestAvailable</RunLevel>") != std::wstring::npos);
    CHECK(task.find(L"<Delay>PT15S</Delay>") != std::wstring::npos);
    CHECK(task.find(L"WinLove\\programs.ps1") != std::wstring::npos);

    const auto json = nlohmann::json::parse(programsJson(plan));
    CHECK(json["texts"]["heading"] == "Programlar kuruluyor"); // the app's text
    CHECK(json["texts"]["close"] == "Close");                  // a key it did not give: English
    REQUIRE(json["programs"].size() == 2);
    CHECK(json["programs"][1]["id"] == "VideoLAN.VLC");
    CHECK(json["programs"][1]["name"] == "VLC media player");

    CHECK(estimatePostSetupSeconds(plan) >= 120);
    plan.programs.push_back({L"bad id&calc", L"x"});
    CHECK(invalidPrograms(plan) == std::vector<std::size_t>{2});
    CHECK_FALSE(applyPostSetup(scratch(L"mount-bad"), plan, {}).has_value());
    plan.programs.pop_back();

    // Into a folder shaped like a mounted image.
    const auto mount = scratch(L"mount");
    std::filesystem::remove_all(mount);
    std::filesystem::create_directories(mount / L"Windows");
    REQUIRE(applyPostSetup(mount, plan, {}).has_value());
    const auto folder = mount / L"Windows" / L"Setup" / L"Scripts" / L"WinLove";
    CHECK(std::filesystem::is_regular_file(folder / L"programs.ps1"));
    CHECK(readAll(folder / L"programs.ps1").find("WinLove Programs") != std::string::npos);
    CHECK(readAll(folder / L"programs.json").find("VideoLAN.VLC") != std::string::npos);
    CHECK(readAll(folder / L"programs-task.xml").starts_with("\xFF\xFE"));
    CHECK(readAll(mount / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd").find("postsetup-machine.cmd") != std::string::npos);
    // An empty plan takes them out again.
    REQUIRE(applyPostSetup(mount, PostSetupPlan{}, {}).has_value());
    CHECK_FALSE(std::filesystem::exists(folder / L"programs.ps1"));
    CHECK_FALSE(std::filesystem::exists(folder / L"programs-task.xml"));
}
