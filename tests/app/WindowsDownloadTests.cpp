// D-093: what "Windows indir" downloads and how it names the ISO.
#include "app/controllers/WindowsDownloadController.h"

#include <doctest.h>

using namespace wl;
using namespace wl::app;
using core::uup::File;
using core::uup::FileKind;

namespace {

File file(std::wstring name, std::uint64_t size) {
    File f;
    f.name = std::move(name);
    f.size = size;
    f.kind = core::uup::fileKind(f.name);
    return f;
}

} // namespace

TEST_CASE("Windows download: the files of a request") {
    core::uup::FileSet set;
    set.files = {file(L"professional_tr-tr.esd", 554), file(L"Microsoft-Windows-Foundation-Package.ESD", 100),
                 file(L"Microsoft-Windows-MediaPlayer-Package-amd64.cab", 10), file(L"Windows11.0-KB5124010-x64.msu", 4689),
                 file(L"Windows11.0-KB5121794-x64.cab", 1), file(L"Edge.wim", 180), file(L"AppInstaller_x64.msix", 59)};
    const auto all = WindowsDownloadController::filesFor(set, /*updates=*/true);
    CHECK(all.size() == 6); // no Store app
    const auto base = WindowsDownloadController::filesFor(set, /*updates=*/false);
    REQUIRE(base.size() == 3); // the edition, its package ESD and the feature cab only
    CHECK(base[0].name == L"professional_tr-tr.esd");
    CHECK(base[2].name == L"Microsoft-Windows-MediaPlayer-Package-amd64.cab");
}

TEST_CASE("Windows download: the ISO's name") {
    core::uup::Build b;
    b.title = L"Windows 11, version 26H2 (26300.9550)";
    b.build = L"26300.9550";
    b.arch = L"amd64";
    CHECK(WindowsDownloadController::isoName(b, L"tr-TR") == L"Win11_26H2_26300.9550_tr-tr_x64.iso");
    b.title = L"Windows 11 Insider Preview 27965.1000 (rs_prerelease)";
    b.build = L"27965.1000";
    b.arch = L"arm64";
    CHECK(WindowsDownloadController::isoName(b, L"en-us") == L"Win11_Insider_27965.1000_en-us_arm64.iso");
    b.title = L"Windows 10, version 22H2 (19045.6456)";
    b.build = L"19045.6456";
    b.arch = L"amd64";
    CHECK(WindowsDownloadController::isoName(b, L"de-de") == L"Win10_22H2_19045.6456_de-de_x64.iso");
}
