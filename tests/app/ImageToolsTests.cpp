// D-058: the Images page tools — the SHA-256 a user pastes, the search / architecture filter, and
// what the tools refuse before any file is touched. The WIM work itself is tested on the lab copy
// (tools/lab_imagetools.ps1).
#include "app/controllers/ImageController.h"
#include "app/pages/images/EditionSelection.h"
#include "core/system/Hash.h"

#include <doctest.h>

#include <filesystem>
#include <mutex>

using namespace wl;
using namespace wl::app;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"imagetools";
    std::filesystem::create_directories(dir);
    return dir / name;
}

core::ImageInfo edition(int index, std::wstring name, std::wstring editionId, core::Architecture arch) {
    core::ImageInfo image;
    image.index = index;
    image.name = std::move(name);
    image.editionId = std::move(editionId);
    image.architecture = arch;
    return image;
}

core::SourceInfo source(core::ImageFormat format, const wchar_t* installImage, bool solid, unsigned parts) {
    core::SourceInfo info;
    info.path = scratch(L"missing") / installImage; // never on disk: nothing here may touch a file
    info.format = format;
    info.installImage = installImage;
    info.install.header.compression = solid ? core::WimCompression::Lzms : core::WimCompression::Lzx;
    info.install.header.solid = solid;
    info.install.header.totalParts = static_cast<decltype(info.install.header.totalParts)>(parts);
    info.install.images.push_back(edition(1, L"Windows 11 Pro", L"Professional", core::Architecture::X64));
    return info;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    std::mutex mutex;
    std::vector<std::function<void()>> posted;
    std::vector<Str> refused;

    ImageController controller{state,
                               ImageController::Events{
                                   [this](std::function<void()> fn) {
                                       std::scoped_lock lock(mutex);
                                       posted.push_back(std::move(fn));
                                   },
                                   [](ImageController::Failure, const Error&, int) {},
                                   [](Str, std::wstring) {},
                                   [this](Str why) { refused.push_back(why); },
                                   [](std::wstring) {},
                                   {},
                                   {},
                               }};
};

} // namespace

TEST_CASE("sha-256: a pasted value is read the way people paste it") {
    const std::wstring hash = L"0e3c966c742d86a08ac3a96cc84928739a3eac09556b60e71c9e6ca419928113";
    CHECK(core::normalizeSha256(hash) == hash);
    CHECK(core::normalizeSha256(L"0E3C966C742D86A08AC3A96CC84928739A3EAC09556B60E71C9E6CA419928113") == hash);
    CHECK(core::normalizeSha256(L"  SHA256: " + hash + L"\r\n") == hash);
    CHECK(core::normalizeSha256(L"0e3c 966c 742d 86a0 8ac3 a96c c849 2873 9a3e ac09 556b 60e7 1c9e 6ca4 1992 8113") == hash);
    CHECK(core::normalizeSha256(hash.substr(1)).empty());          // 63 digits
    CHECK(core::normalizeSha256(hash + L"0").empty());             // 65 digits
    CHECK(core::normalizeSha256(L"zz" + hash.substr(2)).empty());  // not hex
    CHECK(core::normalizeSha256(L"").empty());
}

TEST_CASE("editions filter: text without case over name, edition ID and index; architecture") {
    const auto home = edition(1, L"Windows 11 Home", L"Core", core::Architecture::X64);
    const auto pro = edition(4, L"Windows 11 Pro", L"Professional", core::Architecture::X64);
    const auto arm = edition(2, L"Windows 11 Pro", L"Professional", core::Architecture::Arm64);

    CHECK(EditionFilter{}.matches(home));
    CHECK(EditionFilter{L"   ", L""}.matches(home));
    CHECK(EditionFilter{L"PRO", L""}.matches(pro));
    CHECK_FALSE(EditionFilter{L"pro", L""}.matches(home));
    CHECK(EditionFilter{L"core", L""}.matches(home)); // the edition ID
    CHECK(EditionFilter{L"4", L""}.matches(pro));     // the index
    CHECK(EditionFilter{L"", L"x64"}.matches(pro));
    CHECK_FALSE(EditionFilter{L"", L"x64"}.matches(arm));
    CHECK(EditionFilter{L"pro", L"arm64"}.matches(arm));
    CHECK_FALSE(EditionFilter{L"home", L"arm64"}.matches(arm));
}

TEST_CASE("image tools: refused before any file is touched") {
    Fixture f;
    CHECK(f.controller.toolsRefusal() == Str::ImagesEmptyTitle);

    SUBCASE("a split image is joined first; it is the only one joined") {
        f.state.setSource(source(core::ImageFormat::Wim, L"install.swm", false, 3));
        CHECK(f.controller.isSwmSource());
        CHECK_FALSE(f.controller.toolsRefusal());
        f.controller.recompress(core::WimCompression::Xpress);
        f.controller.splitSwm(scratch(L"out.swm"), 1000);
        f.controller.duplicateEdition(1, L"Kopya");
        CHECK(f.refused == std::vector<Str>{Str::ImagesSwmJoinFirst, Str::ImagesSwmJoinFirst, Str::ImagesDeleteNeedsWim});
    }
    SUBCASE("an ESD is not split, and a plain WIM is not joined") {
        f.state.setSource(source(core::ImageFormat::Esd, L"install.esd", true, 1));
        f.controller.splitSwm(scratch(L"out.swm"), 1000);
        f.controller.mergeSwm(scratch(L"out.wim"));
        CHECK(f.refused == std::vector<Str>{Str::ImagesSplitNeedsWim, Str::ImagesSwmOnly});
    }
    SUBCASE("not while an edition is mounted") {
        f.state.setSource(source(core::ImageFormat::Wim, L"install.wim", false, 1));
        f.state.setMounted(MountedImage{L"C:\\mount", L"C:\\install.wim", 1, L"Windows 11 Pro"});
        CHECK(f.controller.toolsRefusal() == Str::ImagesUnmountFirst);
        f.controller.recompress(core::WimCompression::Xpress);
        f.controller.appendFrom(scratch(L"other.wim"), {1});
        CHECK(f.refused == std::vector<Str>{Str::ImagesUnmountFirst, Str::ImagesUnmountFirst});
    }
    CHECK_FALSE(f.state.operation().has_value()); // nothing was started
}
