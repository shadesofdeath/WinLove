// D-145: the self-update's engine — the asset GitHub's answer offers (the real layout of every release:
// one "WinLove.exe" with a SHA-256 digest), the addresses it may come from, what a file says about
// itself (PE, x64, GUI, version resource), the checks before a swap, the swap and its undo (a failure
// between the two renames, an old file still in use, the rollback). Files only in a temp folder: the
// build's own WinLove.exe is read (copied), never changed. The changelog's parts of a release list.
#include "core/net/ReleaseCheck.h"
#include "core/net/SelfUpdate.h"
#include "core/system/Hash.h"

#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::core;
namespace fs = std::filesystem;

namespace {

fs::path folder(const wchar_t* name) {
    const fs::path dir = fs::temp_directory_path() / L"wl-tests" / L"self-update" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

// The build's WinLove.exe (next to wl_tests.exe): a real x64 GUI program with WinLove's version resource.
fs::path builtExe() {
    wchar_t path[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return fs::path(std::wstring(path, n)).parent_path() / L"WinLove.exe";
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::vector<Release> fixture() {
    auto releases = parseReleases(readFile(fs::path(WL_SOURCE_DIR) / L"tests/integration/fixtures/changelog-sample.json"));
    REQUIRE(releases);
    return *releases;
}

} // namespace

TEST_CASE("D-145: a release's assets and whole body are read from the one answer") {
    const auto releases = fixture();
    REQUIRE(releases.size() == 5); // the draft is not out
    const Release& newest = releases[0];
    CHECK(newest.tag == L"v1.4.0-beta");
    CHECK(newest.body.starts_with(L"**WinLove 1.4.0 Beta**"));
    CHECK(newest.body.find(L"## English") != std::wstring::npos); // the whole body, not the excerpt
    REQUIRE(newest.assets.size() == 1);
    const auto& asset = newest.assets[0];
    CHECK(asset.name == L"WinLove.exe");
    CHECK(asset.size == 11534336);
    CHECK(asset.contentType == L"application/x-msdownload");
    CHECK(asset.sha256 == L"9b1e04c2a4f1b0d6e8c3a7f5b2d9e4c1a6f3b8d0e7c5a2f9b4d1e8c3a6f0b7d2");
    CHECK(asset.uploaded);
    CHECK(releases[1].assets[0].sha256.empty()); // "digest": null
    // GitHub's real v1.3.0-beta: its digest, as the API listed it on 2026-11-11.
    CHECK(releases[2].assets[0].sha256 == L"32a1e589f17f500cef30b1e14b9db7878d9903da49477510711ad11fcbb3763b");
    CHECK(releases[4].body.empty()); // "body": null

    // Odd assets are skipped or kept without what is wrong in them.
    const auto odd = parseReleases(R"([{"tag_name":"v2.0.0","assets":[1,{"name":""},{"name":"x.exe","size":-5,"digest":"md5:abc",
                                       "state":"starter"},{"name":"y.exe","digest":"sha256:XYZ"}]}])");
    REQUIRE(odd);
    REQUIRE((*odd)[0].assets.size() == 2);
    CHECK((*odd)[0].assets[0].size == 0);
    CHECK((*odd)[0].assets[0].sha256.empty());
    CHECK_FALSE((*odd)[0].assets[0].uploaded);
    CHECK((*odd)[0].assets[1].sha256.empty()); // not 64 hex digits
}

TEST_CASE("D-145: the changelog's newer and older releases") {
    const auto check = evaluateReleases(L"1.3.0 Beta", fixture());
    REQUIRE(check.releases.size() == 5);
    CHECK(check.releases[0].tag == L"v1.4.0-beta"); // newest version first
    const auto parts = changelogParts(check, 3);
    REQUIRE(parts.newer.size() == 2);
    CHECK(check.releases[parts.newer[0]].tag == L"v1.4.0-beta");
    CHECK(check.releases[parts.newer[1]].tag == L"v1.3.1-beta");
    REQUIRE(parts.older.size() == 3);
    CHECK(check.releases[parts.older[0]].tag == L"v1.3.0-beta"); // the running one heads the older ones
    CHECK(check.releases[parts.older[2]].tag == L"v1.0.0-alpha");
    CHECK(changelogParts(check, 1).older.size() == 1);
    // Up to date: nothing newer; an unreadable running version: everything is "older".
    CHECK(changelogParts(evaluateReleases(L"1.4.0 Beta", fixture())).newer.empty());
    CHECK(changelogParts(evaluateReleases(L"dev build", fixture()), 10).older.size() == 5);
}

TEST_CASE("D-145: the asset that becomes WinLove.exe, and where it may come from") {
    const auto releases = fixture();
    const auto picked = pickUpdateAsset(releases[0]);
    REQUIRE(picked);
    CHECK(picked->url == L"https://github.com/shadesofdeath/WinLove/releases/download/v1.4.0-beta/WinLove.exe");

    Release r;
    auto asset = [](const wchar_t* name, std::uint64_t size = 1000, std::wstring url = {}) {
        ReleaseAsset a;
        a.name = name;
        a.size = size;
        a.url = url.empty() ? L"https://github.com/shadesofdeath/WinLove/releases/download/v9.0.0/" + std::wstring(name) : url;
        return a;
    };
    r.assets = {asset(L"WinLove-src.zip"), asset(L"WinLove-arm64.exe"), asset(L"WinLove-x64.exe"), asset(L"README.txt")};
    REQUIRE(pickUpdateAsset(r));
    CHECK(pickUpdateAsset(r)->name == L"WinLove-x64.exe"); // no plain WinLove.exe: the x64 one
    r.assets.push_back(asset(L"winlove.EXE"));
    CHECK(pickUpdateAsset(r)->name == L"winlove.EXE"); // the plain name wins (any case)
    r.assets = {asset(L"WinLove.zip"), asset(L"WinLove-arm64.exe"), asset(L"Setup.exe")};
    CHECK_FALSE(pickUpdateAsset(r)); // nothing installable: the app offers the page
    r.assets = {asset(L"WinLove.exe", 0)};
    CHECK_FALSE(pickUpdateAsset(r)); // empty
    r.assets = {asset(L"WinLove.exe", 2ull << 30)};
    CHECK_FALSE(pickUpdateAsset(r)); // larger than any WinLove
    r.assets = {asset(L"WinLove.exe", 1000, L"https://evil.example.com/WinLove.exe")};
    CHECK_FALSE(pickUpdateAsset(r)); // not the project's release download
    r.assets = {asset(L"WinLove.exe")};
    r.assets[0].uploaded = false;
    CHECK_FALSE(pickUpdateAsset(r)); // a broken upload

    CHECK(trustedAssetUrl(L"https://github.com/shadesofdeath/WinLove/releases/download/v1.3.0-beta/WinLove.exe"));
    CHECK(trustedAssetUrl(L"https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sp=r&sig=x"));
    CHECK(trustedAssetUrl(L"https://objects.githubusercontent.com/github-production-release-asset-2e65be/3"));
    for (const wchar_t* bad : {L"http://github.com/shadesofdeath/WinLove/releases/download/v1/WinLove.exe",
                               L"https://github.com/someone/WinLove/releases/download/v1/WinLove.exe",
                               L"https://github.com/shadesofdeath/WinLove/releases/download/../../x",
                               L"https://githubusercontent.com.evil.net/x", L"https://evil.net/.githubusercontent.com/x",
                               L"https://user@objects.githubusercontent.com/x", L"https://objects.githubusercontent.com:8443/x",
                               L"file:///C:/WinLove.exe", L"https://objects.githubusercontent.com/a b", L""}) {
        CHECK_MESSAGE(!trustedAssetUrl(bad), bad);
    }
}

TEST_CASE("D-145: a file is checked before it may take WinLove.exe's place") {
    const fs::path built = builtExe();
    REQUIRE(fs::exists(built));
    const fs::path dir = folder(L"verify");
    const fs::path copy = dir / L"WinLove.exe.new";
    fs::copy_file(built, copy);

    auto facts = readExeFacts(copy, true);
    REQUIRE(facts);
    CHECK(facts->pe);
    CHECK(facts->machine == 0x8664);
    CHECK(facts->pe32Plus);
    CHECK(facts->subsystem == 2);
    CHECK(facts->productVersion == L"" WL_VERSION_LABEL);
    REQUIRE(facts->version);
    CHECK(*facts->version == *parseAppVersion(L"" WL_VERSION_LABEL));
    CHECK(facts->sha256.size() == 64);

    UpdateExpectation expect{facts->size, facts->sha256, parseAppVersion(L"" WL_VERSION_LABEL)};
    auto ok = verifyUpdateFile(copy, expect);
    REQUIRE(ok);
    CHECK_FALSE(ok->failure);

    auto failureOf = [&](UpdateExpectation e) {
        auto v = verifyUpdateFile(copy, e);
        REQUIRE(v);
        return v->failure;
    };
    UpdateExpectation e = expect;
    e.size += 1;
    CHECK(failureOf(e) == VerifyFailure::Size);
    e = expect;
    e.sha256[0] = e.sha256[0] == L'0' ? L'1' : L'0';
    CHECK(failureOf(e) == VerifyFailure::Hash);
    e = expect;
    e.version = parseAppVersion(L"v9.9.9-beta");
    CHECK(failureOf(e) == VerifyFailure::VersionMismatch);
    e = expect;
    e.sha256.clear(); // no digest listed: size, PE and version still decide
    e.size = 0;
    CHECK_FALSE(failureOf(e));

    // Not a program; a program of another kind.
    const fs::path text = dir / L"notes.txt";
    writeFile(text, "MZ but not really a program");
    auto notPe = verifyUpdateFile(text, {});
    REQUIRE(notPe);
    CHECK(notPe->failure == VerifyFailure::NotPe);
    std::string image = readFile(copy);
    const std::uint32_t pe = static_cast<unsigned char>(image[0x3C]) | (static_cast<unsigned char>(image[0x3D]) << 8);
    std::string arm = image;
    arm[pe + 4] = static_cast<char>(0x64); // IMAGE_FILE_MACHINE_ARM64 = 0xAA64
    arm[pe + 5] = static_cast<char>(0xAA);
    writeFile(dir / L"arm.exe", arm);
    CHECK(verifyUpdateFile(dir / L"arm.exe", {})->failure == VerifyFailure::NotX64);
    std::string console = image;
    console[pe + 24 + 68] = 3; // IMAGE_SUBSYSTEM_WINDOWS_CUI
    writeFile(dir / L"console.exe", console);
    CHECK(verifyUpdateFile(dir / L"console.exe", {})->failure == VerifyFailure::NotGui);
    CHECK_FALSE(readExeFacts(dir / L"missing.exe", false));
    CHECK(std::wstring(verifyFailureName(VerifyFailure::Hash)) == L"sha256");
}

TEST_CASE("D-145: the swap, a failure between its two renames, an old file in use, the rollback") {
    const fs::path dir = folder(L"swap");
    const fs::path exe = dir / L"WinLove.exe";
    const UpdatePaths paths = updatePaths(exe);
    CHECK(paths.staged == dir / L"WinLove.exe.new");
    CHECK(paths.backup == dir / L"WinLove.exe.old");
    CHECK(checkFolderWritable(dir));
    CHECK_FALSE(checkFolderWritable(dir / L"no-such-folder"));

    writeFile(exe, "old program");
    writeFile(paths.staged, "new program");
    // A failure between the renames: everything as it was.
    SwapHooks failing;
    failing.afterBackup = [] { return Result<void>{std::unexpected(Error{ErrorCode::IoError, L"simulated"})}; };
    CHECK_FALSE(swapInUpdate(exe, paths.staged, failing));
    CHECK(readFile(exe) == "old program");
    CHECK(readFile(paths.staged) == "new program");
    CHECK_FALSE(fs::exists(paths.backup));

    // The swap.
    REQUIRE(swapInUpdate(exe, paths.staged));
    CHECK(readFile(exe) == "new program");
    CHECK(readFile(paths.backup) == "old program");
    CHECK_FALSE(fs::exists(paths.staged));

    // The new one did not start: back.
    REQUIRE(rollbackUpdate(exe));
    CHECK(readFile(exe) == "old program");
    CHECK(readFile(paths.staged) == "new program");
    CHECK_FALSE(fs::exists(paths.backup));
    CHECK(rollbackUpdate(exe)); // nothing to undo: nothing changes
    CHECK(readFile(exe) == "old program");

    // An earlier update's old file still in use: nothing is changed.
    writeFile(paths.backup, "an old process's file");
    const HANDLE held = CreateFileW(paths.backup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(held != INVALID_HANDLE_VALUE);
    CHECK_FALSE(swapInUpdate(exe, paths.staged));
    CHECK(readFile(exe) == "old program");
    CHECK(readFile(paths.staged) == "new program");
    CHECK_FALSE(removeUpdateBackup(exe)); // in use: stays for the next start
    CloseHandle(held);
    // Gone now: the stale one is replaced.
    REQUIRE(swapInUpdate(exe, paths.staged));
    CHECK(readFile(paths.backup) == "old program");
    CHECK(removeUpdateBackup(exe));
    CHECK_FALSE(fs::exists(paths.backup));
    CHECK(removeUpdateBackup(exe)); // never there: done

    // Nothing to swap in / nothing to replace.
    CHECK_FALSE(swapInUpdate(exe, dir / L"missing.new"));
    CHECK_FALSE(swapInUpdate(dir / L"missing.exe", exe));
}

TEST_CASE("D-145: a download refuses an address that is not the project's") {
    ReleaseAsset asset;
    asset.name = L"WinLove.exe";
    asset.size = 10;
    asset.url = L"https://example.com/WinLove.exe";
    const fs::path dir = folder(L"download");
    auto got = downloadUpdate(asset, dir / L"WinLove.exe", L"WinLove/test", TaskContext{});
    REQUIRE_FALSE(got);
    CHECK(got.error().code == ErrorCode::AccessDenied);
    CHECK_FALSE(fs::exists(dir / L"WinLove.exe.new")); // nothing was asked for, nothing written
}
