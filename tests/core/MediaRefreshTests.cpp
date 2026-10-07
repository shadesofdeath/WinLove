#include "core/image/MediaRefresh.h"

#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace wl::core;

namespace {
void touch(const std::filesystem::path& file, const char* text = "x") {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}
const MediaFile* find(const std::vector<MediaFile>& files, std::wstring_view path) {
    const auto it = std::ranges::find_if(files, [&](const MediaFile& f) { return _wcsicmp(f.path.c_str(), std::wstring(path).c_str()) == 0; });
    return it == files.end() ? nullptr : &*it;
}
} // namespace

TEST_CASE("media refresh: the Setup dynamic update, then Setup and the boot manager from the updated boot.wim (D-080)") {
    const auto root = std::filesystem::temp_directory_path() / L"wl-media-refresh-test";
    std::filesystem::remove_all(root);
    const auto media = root / L"media";
    touch(media / L"sources" / L"setup.exe");
    touch(media / L"sources" / L"setuphost.exe");
    touch(media / L"sources" / L"appraiser.dll");
    touch(media / L"sources" / L"tr-tr" / L"setup.exe.mui");
    touch(media / L"bootmgr.efi");
    touch(media / L"efi" / L"boot" / L"bootx64.efi");
    touch(media / L"efi" / L"microsoft" / L"boot" / L"bcd");
    const auto du = root / L"du";
    touch(du / L"appraiser.dll");
    touch(du / L"newcomponent.dll");
    touch(du / L"tr-tr" / L"setup.exe.mui");   // the media has Turkish: taken
    touch(du / L"ar-sa" / L"AcRes.dll.mui");   // and no Arabic: left out
    touch(du / L"setup.exe"); // the updated image's own copy must win over the dynamic update's
    const auto saved = root / L"saved";
    for (const wchar_t* name : {L"bootmgfw.efi", L"bootmgr.efi", L"boot.stl"}) {
        touch(saved / name, "new");
    }
    for (const wchar_t* name : {L"setup.exe", L"setuphost.exe", L"setupplatform.dll"}) {
        touch(saved / L"sources" / name, "new");
    }
    touch(saved / L"sources" / L"en-US" / L"setup.exe.mui", "new"); // a folder the media lacks: new

    const auto files = mediaRefreshFiles(media, du, saved);
    REQUIRE(find(files, L"sources\\appraiser.dll"));
    CHECK_FALSE(find(files, L"sources\\appraiser.dll")->isNew);
    CHECK(find(files, L"sources\\appraiser.dll")->file == du / L"appraiser.dll");
    REQUIRE(find(files, L"sources\\newcomponent.dll"));
    CHECK(find(files, L"sources\\newcomponent.dll")->isNew);
    REQUIRE(find(files, L"sources\\setup.exe"));
    CHECK(find(files, L"sources\\setup.exe")->file == saved / L"sources" / L"setup.exe");
    CHECK(find(files, L"sources\\setuphost.exe")->file == saved / L"sources" / L"setuphost.exe");
    REQUIRE(find(files, L"sources\\setupplatform.dll"));
    CHECK(find(files, L"sources\\setupplatform.dll")->isNew);
    REQUIRE(find(files, L"sources\\en-US\\setup.exe.mui"));
    CHECK(find(files, L"sources\\en-US\\setup.exe.mui")->isNew);
    REQUIRE(find(files, L"efi\\boot\\bootx64.efi"));
    CHECK(find(files, L"efi\\boot\\bootx64.efi")->file == saved / L"bootmgfw.efi");
    REQUIRE(find(files, L"bootmgr.efi"));
    CHECK(find(files, L"bootmgr.efi")->file == saved / L"bootmgr.efi");
    REQUIRE(find(files, L"efi\\microsoft\\boot\\boot.stl"));
    CHECK(find(files, L"efi\\microsoft\\boot\\boot.stl")->isNew);
    CHECK_FALSE(find(files, L"efi\\microsoft\\boot\\bcd")); // not ours to touch
    REQUIRE(find(files, L"sources\\tr-tr\\setup.exe.mui"));
    CHECK_FALSE(find(files, L"sources\\tr-tr\\setup.exe.mui")->isNew);
    CHECK_FALSE(find(files, L"sources\\ar-sa\\AcRes.dll.mui"));
    CHECK(files.size() == 10);

    // Nothing given: nothing replaced.
    CHECK(mediaRefreshFiles(media, {}, {}).empty());
    std::filesystem::remove_all(root);
}
