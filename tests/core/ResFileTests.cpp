// 7TSP icon packs: .res files (read / written), Pack.ini, conversion to WinLove's pack layout and
// archives opened with Windows' tar.exe. Everything is written under %TEMP%\wl-tests.
#include "base/File.h"
#include "core/image/icons/IconGroups.h"
#include "core/image/icons/ResFile.h"
#include "core/system/Process.h"

#include <doctest.h>

#include <cstring>
#include <filesystem>

using namespace wl;
using namespace wl::core;

namespace {

const std::filesystem::path kBrand = std::filesystem::path(WL_SOURCE_DIR) / L"resources" / L"brand" / L"WinLove.ico";

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"resfile" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

std::vector<IconImage> brandImages() {
    auto bytes = readFileBytes(kBrand);
    REQUIRE(bytes);
    auto images = parseIco(*bytes);
    REQUIRE(images);
    return std::move(*images);
}

// A tree like a 7TSP .res: one RT_ICON per image, RT_GROUP_ICON directories pointing at them.
ResourceTree iconTree(const std::vector<std::pair<ResourceKey, std::vector<IconImage>>>& groups) {
    ResourceTree tree;
    tree.ensureType(kRtIcon);
    tree.ensureType(kRtGroupIcon); // both first: adding a type moves the others
    auto& icons = *tree.type(kRtIcon);
    auto& dirs = *tree.type(kRtGroupIcon);
    std::uint16_t next = 1;
    for (const auto& [key, images] : groups) {
        std::string dir(6, '\0');
        const std::uint16_t head[3] = {0, 1, static_cast<std::uint16_t>(images.size())};
        std::memcpy(dir.data(), head, 6);
        for (const auto& img : images) {
            const std::uint16_t id = next++;
            ensureName(icons, id).languages.push_back({0x409, 0, img.data});
            char e[14]{};
            e[0] = static_cast<char>(img.width >= 256 ? 0 : img.width);
            e[1] = static_cast<char>(img.height >= 256 ? 0 : img.height);
            e[2] = static_cast<char>(img.colors);
            std::memcpy(e + 4, &img.planes, 2);
            std::memcpy(e + 6, &img.bitCount, 2);
            const auto size = static_cast<std::uint32_t>(img.data.size());
            std::memcpy(e + 8, &size, 4);
            std::memcpy(e + 12, &id, 2);
            dir.append(e, 14);
        }
        dirs.names.push_back(ResourceName{key, {}, {{0x409, 0, dir}}});
    }
    return tree;
}

} // namespace

TEST_CASE("7tsp: a .res file round-trips and gives the same icon groups") {
    const auto images = brandImages();
    const auto tree = iconTree({{ResourceKey{3, {}}, images}, {ResourceKey{0, L"ICO_MYCOMPUTER"}, images}});
    const std::string res = writeResFile(tree);
    auto back = parseResFile(res);
    REQUIRE(back);
    const auto groups = listIconGroups(*back);
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].key.name == L"ICO_MYCOMPUTER"); // named first, as in a PE directory
    CHECK(groups[1].key == ResourceKey{3, {}});
    REQUIRE(groups[1].images.size() == images.size());
    for (std::size_t i = 0; i < images.size(); ++i) {
        CHECK(groups[1].images[i].data == images[i].data);
        CHECK(groups[1].images[i].width == images[i].width);
    }
    CHECK(writeResFile(*back) == res);

    // Cut anywhere inside an entry: refused, never read past the end.
    CHECK_FALSE(parseResFile(res.substr(0, res.size() / 2)));
    std::string huge = res;
    const std::uint32_t big = 0x7FFFFFFF;
    std::memcpy(huge.data() + 32, &big, 4); // the second entry's DataSize
    CHECK_FALSE(parseResFile(huge));
    CHECK(parseResFile("")); // an empty file is an empty tree
}

TEST_CASE("7tsp: group keys as pack file names") {
    CHECK(packFileStem(ResourceKey{3, {}}) == L"3");
    CHECK(packFileStem(ResourceKey{0, L"SHIDI_SHIELD"}) == L"SHIDI_SHIELD");
    CHECK(packFileStem(ResourceKey{0, L"A:B"}).empty());
    CHECK(packFileStem(ResourceKey{0, L"123"}).empty()); // would read back as id 123
    CHECK(packFileStem(ResourceKey{0, L"NAME."}).empty());
}

TEST_CASE("7tsp: a pack folder is read and converted to WinLove's layout") {
    const auto dir = scratch(L"pack");
    const auto root = dir / L"Lumicons"; // archives often wrap the pack in a folder
    std::filesystem::create_directories(root / L"Resources");
    REQUIRE(writeFileAtomic(root / L"Pack.ini", "[Base Pack]\r\nPack=Test Symbols\r\nBase by=someone\r\nversion=2.0\r\n"));
    const auto images = brandImages();
    REQUIRE(writeFileAtomic(root / L"Resources" / L"imageres.dll.mun.res",
                            writeResFile(iconTree({{ResourceKey{3, {}}, images}, {ResourceKey{0, L"BAD:NAME"}, images}}))));
    REQUIRE(writeFileAtomic(root / L"Resources" / L"broken.dll.res", "not a res file at all, just text"));
    REQUIRE(writeFileAtomic(root / L"Resources" / L"readme.txt", "ignored"));

    CHECK(is7tspPack(dir));
    CHECK_FALSE(is7tspPack(scratch(L"not-a-pack")));
    auto pack = read7tspPack(dir);
    REQUIRE(pack);
    CHECK(pack->name == L"Test Symbols");
    CHECK(pack->author == L"someone");
    REQUIRE(pack->files.size() == 2);
    CHECK(pack->files[1].target == L"imageres.dll.mun");

    const auto out = dir / L"converted";
    std::vector<std::wstring> skipped;
    auto written = convert7tspPack(*pack, out, &skipped);
    REQUIRE(written);
    CHECK(*written == 1);
    CHECK(skipped.size() == 2); // broken.dll (not a .res) and the group with an unsafe name
    auto ico = readFileBytes(out / L"imageres.dll.mun" / L"3.ico");
    REQUIRE(ico);
    auto back = parseIco(*ico);
    REQUIRE(back);
    CHECK(back->size() == images.size());
    CHECK(std::filesystem::exists(out / L"iconpack.json"));

    auto none = read7tspPack(scratch(L"empty"));
    CHECK_FALSE(none);
}

TEST_CASE("7tsp: archives open with Windows' tar.exe and stay inside the target folder") {
    const auto tar = systemTool(L"tar.exe");
    if (!tar || !std::filesystem::exists(*tar)) {
        return;
    }
    const auto dir = scratch(L"archive");
    std::filesystem::create_directories(dir / L"src" / L"Resources");
    REQUIRE(writeFileAtomic(dir / L"src" / L"Pack.ini", "[Base Pack]\nPack=Zipped\n"));
    REQUIRE(writeFileAtomic(dir / L"src" / L"Resources" / L"shell32.dll.mun.res",
                            writeResFile(iconTree({{ResourceKey{4, {}}, brandImages()}}))));
    const auto zip = dir / L"pack.zip";
    auto made = runProcess(L"\"" + *tar + L"\" -a -c -f \"" + zip.wstring() + L"\" -C \"" + (dir / L"src").wstring() + L"\" Pack.ini Resources", {});
    REQUIRE(made);
    REQUIRE(*made == 0);

    const auto out = dir / L"out";
    REQUIRE(extractArchive(zip, out));
    auto pack = read7tspPack(out);
    REQUIRE(pack);
    CHECK(pack->name == L"Zipped");
    CHECK(pack->files.size() == 1);

    CHECK_FALSE(extractArchive(dir / L"missing.7z", dir / L"out2"));
    REQUIRE(writeFileAtomic(dir / L"fake.7z", "this is not an archive"));
    CHECK_FALSE(extractArchive(dir / L"fake.7z", dir / L"out3"));
}
