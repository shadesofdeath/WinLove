// Removing editions from a WIM: the index arithmetic and the refusals. The rewrite itself needs
// a real image (capturing a WIM takes admin): tools/lab_editions.ps1 runs it on the lab copy.
#include "core/image/ImageInfo.h"
#include "core/image/SetupMedia.h"
#include "core/image/WindowsRelease.h"
#include "core/image/wim/WimGapi.h"

#include <doctest.h>

#include <filesystem>
#include <vector>

using namespace wl;
using namespace wl::core;

TEST_CASE("indexAfterRemoval: what stays is renumbered from 1") {
    // Six editions, Home Single Language (2) goes: Pro moves from 4 to 3.
    const std::vector<int> one{2};
    CHECK(indexAfterRemoval(1, one) == 1);
    CHECK(indexAfterRemoval(2, one) == std::nullopt);
    CHECK(indexAfterRemoval(4, one) == 3);
    CHECK(indexAfterRemoval(6, one) == 5);

    // "Keep only Pro": everything but 4 goes, in any order, duplicates ignored.
    const std::vector<int> others{6, 1, 2, 3, 5, 5};
    CHECK(indexAfterRemoval(4, others) == 1);
    CHECK(indexAfterRemoval(5, others) == std::nullopt);

    CHECK(indexAfterRemoval(3, {}) == 3);
}

TEST_CASE("reorderImages: the new order names every edition once (D-077)") {
    CHECK(isPermutation(std::vector<int>{2, 1, 3}, 3));
    CHECK(isPermutation(std::vector<int>{1}, 1));
    CHECK_FALSE(isPermutation(std::vector<int>{1, 2}, 3));    // one left out
    CHECK_FALSE(isPermutation(std::vector<int>{1, 1, 2}, 3)); // twice
    CHECK_FALSE(isPermutation(std::vector<int>{0, 1, 2}, 3)); // out of range
    CHECK_FALSE(isPermutation(std::vector<int>{1, 2, 4}, 3));
    const auto missing = std::filesystem::temp_directory_path() / L"wl-tests" / L"no-such-image.wim";
    CHECK_FALSE(reorderImages(missing, std::vector<int>{2, 1}, {}));
}

TEST_CASE("AIO: an added edition named like one already there gets its release (D-077)") {
    auto image = [](int index, const wchar_t* name, int build, int sp) {
        ImageInfo i;
        i.index = index;
        i.name = name;
        i.major = 10;
        i.build = build;
        i.spBuild = sp;
        return i;
    };
    const std::vector<ImageInfo> images{
        image(1, L"Windows 11 Pro", 26200, 8037),  // already there
        image(2, L"Windows 10 Pro", 19045, 3803),  // added, unique
        image(3, L"windows 11 pro", 26100, 1742),  // added, same name (case aside), 24H2
        image(4, L"Windows 11 Pro", 26200, 7000),  // added, same release as 1: the version tells them apart
        image(5, L"Windows 11 Pro", 26200, 8037),  // added, the very same Windows: left alone
    };
    const auto renames = distinctEditionNames(images, 2);
    REQUIRE(renames.size() == 2);
    CHECK(renames[0] == std::pair<int, std::wstring>{3, L"windows 11 pro (24H2)"});
    CHECK(renames[1] == std::pair<int, std::wstring>{4, L"Windows 11 Pro (10.0.26200.7000)"});
    CHECK(distinctEditionNames(images, 6).empty()); // nothing added
}

TEST_CASE("isWindowsEdition: a Media Creation Tool ESD offers only its Windows editions") {
    // The seven images of the Windows 10 22H2 MCT ESD in build\lab\win10, as its XML has them.
    auto image = [](const wchar_t* name, const wchar_t* edition, const wchar_t* type, Architecture arch) {
        ImageInfo i;
        i.name = name;
        i.editionId = edition;
        i.installationType = type;
        i.architecture = arch;
        return i;
    };
    CHECK_FALSE(isWindowsEdition(image(L"Windows Setup Media", L"", L"", Architecture::Unknown)));
    CHECK_FALSE(isWindowsEdition(image(L"Microsoft Windows PE (x64)", L"WindowsPE", L"WindowsPE", Architecture::X64)));
    CHECK_FALSE(isWindowsEdition(image(L"Microsoft Windows Setup (x64)", L"WindowsPE", L"WindowsPE", Architecture::X64)));
    CHECK(isWindowsEdition(image(L"Windows 10 Home", L"Core", L"Client", Architecture::X64)));
    CHECK(isWindowsEdition(image(L"Windows 10 Pro", L"Professional", L"Client", Architecture::X64)));
    CHECK(isWindowsEdition(image(L"Windows Server 2025 Standard", L"ServerStandard", L"Server Core", Architecture::X64)));
    CHECK_FALSE(isWindowsEdition(image(L"data", L"", L"", Architecture::Unknown))); // a captured data folder
}

TEST_CASE("enablementBuild: Windows 10 22H2 media keeps 19041 in its XML (D-077, measured)") {
    // build\lab\win10\win10_22h2_tr_consumer.esd, edition 7, Windows\servicing\Packages (read-only mount):
    // the payloads of every release since 20H2, but only 22H2's switch; its SOFTWARE hive says 19045 / 22H2.
    const std::vector<std::wstring> packages{
        L"Microsoft-Windows-20H2Enablement-Payload-Package~31bf3856ad364e35~amd64~~10.0.19041.1799.mum",
        L"Microsoft-Windows-21H1Enablement-Payload-Package~31bf3856ad364e35~amd64~~10.0.19041.1799.mum",
        L"Microsoft-Windows-21H2Enablement-Payload-Package~31bf3856ad364e35~amd64~~10.0.19041.1799.mum",
        L"Microsoft-Windows-22H2Enablement-Package~31bf3856ad364e35~amd64~~10.0.19041.1799.mum",
        L"Microsoft-Windows-22H2Enablement-Payload-Package~31bf3856ad364e35~amd64~~10.0.19041.1799.mum",
        L"Microsoft-Windows-Product-Data-22h2-EKB-Package~31bf3856ad364e35~amd64~~10.0.19041.3803.mum",
        L"Microsoft-Windows-UpdateTargeting-ClientOS-22h2-EKB-Package~31bf3856ad364e35~amd64~~10.0.19041.3803.mum",
    };
    CHECK(enablementBuild(packages) == 19045);
    CHECK(releaseLabel(enablementBuild(packages)) == L"10 22H2");
    // 21H2 media: its switch only; payloads alone switch nothing on.
    CHECK(enablementBuild(std::vector<std::wstring>{
              L"Microsoft-Windows-21H2Enablement-Package~31bf3856ad364e35~amd64~~10.0.19041.1288.mum",
              L"Microsoft-Windows-22H2Enablement-Payload-Package~31bf3856ad364e35~amd64~~10.0.19041.1288.mum"}) == 19044);
    CHECK(enablementBuild(std::vector<std::wstring>{
              L"microsoft-windows-20h2enablement-package~31bf3856ad364e35~amd64~~10.0.19041.572.mum"}) == 19042);
    CHECK(enablementBuild(std::vector<std::wstring>{
              L"Microsoft-Windows-20H2Enablement-Payload-Package~31bf3856ad364e35~amd64~~10.0.19041.1799.mum"}) == 0);
    CHECK(enablementBuild(std::vector<std::wstring>{}) == 0);
    // Windows 11's own enablement packages (23H2, 25H2) are no business of this: their media say the build.
    CHECK(enablementBuild(std::vector<std::wstring>{
              L"Microsoft-Windows-23H2Enablement-Package~31bf3856ad364e35~amd64~~10.0.22621.2506.mum"}) == 0);
}

TEST_CASE("AIO: what the setup media can install (D-077, measured)") {
    auto image = [](int index, int build) {
        ImageInfo i;
        i.index = index;
        i.name = build < 22000 ? L"Windows 10 Pro" : L"Windows 11 Pro";
        i.build = build;
        return i;
    };
    SourceInfo aio;
    aio.install.images = {image(1, 26200), image(2, 19045), image(3, 22631)};
    CHECK(setupMediaBuild(aio) == 0);           // a bare image: no media to judge
    CHECK(editionsMediaCannotInstall(aio).empty());
    CHECK_FALSE(mediaOpensPreviousSetup(aio));

    WimFile boot;
    boot.images = {image(1, 26100), image(2, 26100)};
    boot.header.bootIndex = 2;
    aio.boot = boot;                            // Windows 11 24H2 media
    CHECK(setupMediaBuild(aio) == 26100);
    CHECK(editionsMediaCannotInstall(aio) == std::vector<int>{2});
    CHECK_FALSE(mediaOpensPreviousSetup(aio));  // the new Setup: the switch means something

    aio.boot->images = {image(1, 19041), image(2, 19041)}; // Windows 10 media installs them all
    CHECK(editionsMediaCannotInstall(aio).empty());
    CHECK(mediaOpensPreviousSetup(aio));
    aio.boot->images = {image(1, 22631)};                  // 23H2: no measurement says otherwise
    aio.boot->header.bootIndex = 0;                        // no boot index: the last image
    CHECK(setupMediaBuild(aio) == 22631);
    CHECK(editionsMediaCannotInstall(aio).empty());
}

TEST_CASE("removeImages: a missing file is an error, not a silent success") {
    const auto missing = std::filesystem::temp_directory_path() / L"wl-tests" / L"no-such-image.wim";
    const std::vector<int> indexes{1};
    const auto removed = removeImages(missing, indexes, TaskContext{});
    CHECK_FALSE(removed.has_value());
}
