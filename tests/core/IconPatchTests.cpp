// D-068: icons inside PE files — our parser / writer against Windows' own files (copies in memory,
// nothing on disk is changed except scratch files) and against hand-made trees.
#include "base/File.h"
#include "core/image/icons/IconPatch.h"
#include "core/image/icons/IconSource.h"
#include "core/system/Picture.h"

#include <doctest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

using namespace wl;
using namespace wl::core;

namespace {

const std::filesystem::path kImageres = L"C:\\Windows\\SystemResources\\imageres.dll.mun";
const std::filesystem::path kShell32 = L"C:\\Windows\\SystemResources\\shell32.dll.mun";
const std::filesystem::path kExplorer = L"C:\\Windows\\explorer.exe";

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"icons";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string bytesOf(const std::filesystem::path& file) {
    auto bytes = readFileBytes(file);
    REQUIRE(bytes);
    return std::move(*bytes);
}

const IconGroupInfo& groupOf(const std::vector<IconGroupInfo>& groups, std::uint16_t id) {
    const auto it = std::ranges::find(groups, ResourceKey{id, {}}, &IconGroupInfo::key);
    REQUIRE(it != groups.end());
    return *it;
}

std::size_t iconLeaves(const ResourceTree& tree) {
    const auto* icons = tree.type(kRtIcon);
    return icons ? icons->names.size() : 0;
}

// A 1×1 32 bpp DIB icon image (BITMAPINFOHEADER, height doubled, XOR + AND rows).
IconImage tinyDib(std::uint8_t shade) {
    IconImage img;
    std::string& d = img.data;
    d.resize(40 + 4 + 4, '\0');
    const std::uint32_t header = 40;
    const std::int32_t w = 1;
    const std::int32_t h = 2;
    const std::uint16_t planes = 1;
    const std::uint16_t bpp = 32;
    std::memcpy(d.data(), &header, 4);
    std::memcpy(d.data() + 4, &w, 4);
    std::memcpy(d.data() + 8, &h, 4);
    std::memcpy(d.data() + 12, &planes, 2);
    std::memcpy(d.data() + 14, &bpp, 2);
    d[40] = static_cast<char>(shade);
    d[43] = static_cast<char>(0xFF);
    REQUIRE(describeIconImage(img));
    return img;
}

} // namespace

TEST_CASE("icons: Windows' .mun files parse as resource-only PEs with hundreds of icon groups") {
    if (!std::filesystem::exists(kImageres)) {
        MESSAGE("no imageres.dll.mun on this PC");
        return;
    }
    auto pe = PeImage::parse(bytesOf(kImageres));
    REQUIRE(pe);
    CHECK_FALSE(pe->hasCode());
    CHECK(pe->hasResources());
    const auto groups = listIconGroups(pe->resources());
    CHECK(groups.size() > 300);
    const auto& three = groupOf(groups, 3);
    CHECK(three.images.size() >= 4);
    CHECK(std::ranges::any_of(three.images, [](const IconImage& i) { return i.width == 256 && i.png; }));
    CHECK(std::ranges::any_of(three.images, [](const IconImage& i) { return i.width == 16 && !i.png; }));
    // Our checksum is Windows' (the files carry a correct one).
    const auto& raw = pe->bytes();
    std::uint32_t lfanew = 0;
    std::uint32_t stored = 0;
    std::memcpy(&lfanew, raw.data() + 0x3C, 4);
    std::memcpy(&stored, raw.data() + lfanew + 24 + 64, 4);
    if (stored != 0) {
        CHECK(peChecksum(raw, lfanew + 24 + 64) == stored);
    }
}

TEST_CASE("icons: an unchanged tree is rebuilt with every resource identical, and the rebuild is stable") {
    if (!std::filesystem::exists(kShell32)) {
        return;
    }
    auto pe = PeImage::parse(bytesOf(kShell32));
    REQUIRE(pe);
    auto once = pe->build();
    REQUIRE(once);
    auto again = PeImage::parse(*once);
    REQUIRE(again);
    CHECK(again->resources().leafCount() == pe->resources().leafCount());
    const auto a = listIconGroups(pe->resources());
    const auto b = listIconGroups(again->resources());
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].key == b[i].key);
        CHECK(std::ranges::equal(a[i].images, b[i].images, [](const IconImage& x, const IconImage& y) { return x.data == y.data; }));
    }
    auto twice = again->build();
    REQUIRE(twice);
    CHECK(*twice == *once);
}

TEST_CASE("icons: a group grows and shrinks; ids only it used go, the others stay; Windows loads the result") {
    if (!std::filesystem::exists(kImageres)) {
        return;
    }
    const std::string original = bytesOf(kImageres);
    auto pe = PeImage::parse(original);
    REQUIRE(pe);
    const auto groups = listIconGroups(pe->resources());
    const auto big = groupOf(groups, 3).images;   // 8 images
    const auto small = std::vector<IconImage>{tinyDib(0x40)};
    const std::size_t leaves = iconLeaves(pe->resources());

    // #3 → one image: seven of its RT_ICONs are dropped.
    auto shrunk = patchIconBytes(original, {{ResourceKey{3, {}}, small}});
    REQUIRE(shrunk);
    auto shrunkPe = PeImage::parse(*shrunk);
    REQUIRE(shrunkPe);
    CHECK(iconLeaves(shrunkPe->resources()) == leaves - (big.size() - 1));
    CHECK(groupOf(listIconGroups(shrunkPe->resources()), 3).images.size() == 1);

    // Then the one-image group grows back to eight: fresh ids above the highest.
    auto grown = patchIconBytes(*shrunk, {{ResourceKey{3, {}}, big}});
    REQUIRE(grown);
    auto grownPe = PeImage::parse(*grown);
    REQUIRE(grownPe);
    CHECK(iconLeaves(grownPe->resources()) == leaves);
    CHECK(std::ranges::equal(groupOf(listIconGroups(grownPe->resources()), 3).images, big,
                             [](const IconImage& x, const IconImage& y) { return x.data == y.data; }));

    const auto file = scratch(L"imageres.grown.mun");
    REQUIRE(writeFileAtomic(file, *grown));
    auto check = verifyIconFileWithWindows(file);
    REQUIRE(check);
    CHECK(check->groups == static_cast<int>(groups.size()));
    CHECK(check->failed == 0);

    CHECK_FALSE(patchIconBytes(original, {{ResourceKey{65000, {}}, small}})); // no such group
    CHECK_FALSE(patchIconBytes(original, {{ResourceKey{3, {}}, {}}}));        // no images
}

TEST_CASE("icons: an icon two groups share is not overwritten when one of them changes") {
    ResourceTree tree;
    auto& icons = tree.ensureType(kRtIcon);
    ensureName(icons, 1).languages.push_back({1033, 0, tinyDib(0x10).data});
    ensureName(icons, 2).languages.push_back({1033, 0, tinyDib(0x20).data});
    auto& groups = tree.ensureType(kRtGroupIcon);
    // Hand-made directories: group 10 → icons 1, 2; group 11 → icon 2.
    auto dir = [](std::vector<std::uint16_t> ids) {
        std::string d(6 + ids.size() * 14, '\0');
        d[2] = 1;
        d[4] = static_cast<char>(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) {
            d[6 + i * 14] = 1;
            d[6 + i * 14 + 1] = 1;
            d[6 + i * 14 + 4] = 1;
            d[6 + i * 14 + 6] = 32;
            std::memcpy(d.data() + 6 + i * 14 + 12, &ids[i], 2);
        }
        return d;
    };
    ensureName(groups, 10).languages.push_back({1033, 0, dir({1, 2})});
    ensureName(groups, 11).languages.push_back({1033, 0, dir({2})});
    const auto before11 = groupOf(listIconGroups(tree), 11).images.front().data;

    REQUIRE(replaceIconGroup(tree, ResourceKey{10, {}}, {tinyDib(0x77), tinyDib(0x88)}));
    const auto after = listIconGroups(tree);
    CHECK(groupOf(after, 11).images.front().data == before11); // the shared icon kept its picture
    CHECK(groupOf(after, 10).images[0].data == tinyDib(0x77).data);
    CHECK(groupOf(after, 10).images[1].data == tinyDib(0x88).data);
    CHECK(tree.type(kRtIcon)->names.size() == 3); // 1 reused, 2 kept for group 11, a fresh one
}

TEST_CASE("icons: .ico files are read with bounds checks and their sizes from the data") {
    const std::vector<IconImage> images{tinyDib(1), tinyDib(2)};
    const std::string ico = makeIco(images);
    auto back = parseIco(ico);
    REQUIRE(back);
    REQUIRE(back->size() == 2);
    CHECK((*back)[0].data == images[0].data);
    CHECK((*back)[1].width == 1);
    CHECK((*back)[1].bitCount == 32);

    CHECK_FALSE(parseIco(""));
    CHECK_FALSE(parseIco(ico.substr(0, 20)));        // image cut off
    std::string wrongType = ico;
    wrongType[2] = 2;                                 // a cursor
    CHECK_FALSE(parseIco(wrongType));
    std::string badOffset = ico;
    badOffset[6 + 12] = static_cast<char>(0xFF);     // first image offset far out
    badOffset[6 + 13] = static_cast<char>(0xFF);
    CHECK_FALSE(parseIco(badOffset));
    std::string garbage = ico;
    std::memset(garbage.data() + 6 + 2 * 16, 'x', 40); // first image is neither PNG nor DIB
    CHECK_FALSE(parseIco(garbage));
}

TEST_CASE("icons: only resource containers under Windows\\ are patched; never servicing, boot or program code") {
    CHECK(checkIconPatchPath(L"Windows\\SystemResources\\imageres.dll.mun"));
    CHECK(checkIconPatchPath(L"\\Windows/SystemResources/shell32.dll.mun"));
    CHECK_FALSE(checkIconPatchPath(L"Windows\\WinSxS\\amd64_x\\imageres.dll.mun"));
    CHECK_FALSE(checkIconPatchPath(L"Windows\\servicing\\x.dll"));
    CHECK_FALSE(checkIconPatchPath(L"Windows\\Boot\\EFI\\bootmgfw.efi"));
    CHECK_FALSE(checkIconPatchPath(L"Windows\\System32\\drivers\\x.sys"));
    CHECK_FALSE(checkIconPatchPath(L"Windows\\..\\Users\\x.dll"));
    CHECK_FALSE(checkIconPatchPath(L"Program Files\\App\\app.exe"));
    CHECK_FALSE(checkIconPatchPath(L"Windows\\SystemResources\\readme.txt"));
    CHECK(iconBackupPath(L"Windows\\SystemResources\\imageres.dll.mun") ==
          L"Windows\\WinLove\\IconBackup\\SystemResources\\imageres.dll.mun");

    if (std::filesystem::exists(kExplorer)) {
        auto explorer = PeImage::parse(bytesOf(kExplorer));
        REQUIRE(explorer);
        CHECK(explorer->hasCode());
        CHECK_FALSE(checkIconPatchTarget(L"Windows\\explorer.exe", *explorer));
        // The writer itself copes with a resource section that is not the last one (a new section),
        // and Windows still loads every icon.
        const auto groups = listIconGroups(explorer->resources());
        REQUIRE_FALSE(groups.empty());
        auto patched = patchIconBytes(explorer->bytes(), {{groups.front().key, {tinyDib(0x55)}}});
        REQUIRE(patched);
        auto again = PeImage::parse(*patched);
        REQUIRE(again);
        CHECK(again->sections().size() == explorer->sections().size() + 1);
        CHECK_FALSE(again->hasEmbeddedSignature());
        const auto file = scratch(L"explorer.patched.exe");
        REQUIRE(writeFileAtomic(file, *patched));
        auto check = verifyIconFileWithWindows(file);
        REQUIRE(check);
        CHECK(check->failed == 0);
    }
    if (std::filesystem::exists(kImageres)) {
        auto mun = PeImage::parse(bytesOf(kImageres));
        REQUIRE(mun);
        CHECK(checkIconPatchTarget(L"Windows\\SystemResources\\imageres.dll.mun", *mun));
    }
}

TEST_CASE("icons: not a PE, or a PE pointing outside itself, is refused") {
    CHECK_FALSE(PeImage::parse("hello"));
    CHECK_FALSE(PeImage::parse(std::string(4096, '\0')));
    if (std::filesystem::exists(kShell32)) {
        std::string cut = bytesOf(kShell32);
        cut.resize(cut.size() / 2); // the resource section now runs past the end
        CHECK_FALSE(PeImage::parse(cut));
    }
}

TEST_CASE("icons: a picture becomes Windows' eight sizes (bitmaps + a 256 px PNG) that Windows loads") {
    // A 300×200 picture: fitted into squares with transparent margins.
    std::string pixels(300 * 300 * 4, '\0');
    for (int y = 50; y < 250; ++y) {
        for (int x = 0; x < 300; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * 300 + x) * 4;
            pixels[at] = static_cast<char>(x % 256);
            pixels[at + 1] = static_cast<char>(y % 256);
            pixels[at + 2] = static_cast<char>(0x80);
            pixels[at + 3] = static_cast<char>(0xFF);
        }
    }
    auto png = encodePngBgra(pixels, 300);
    REQUIRE(png);
    const auto file = scratch(L"picture.png");
    REQUIRE(writeFileAtomic(file, *png));
    auto images = loadIconSource(file);
    REQUIRE(images);
    REQUIRE(images->size() == std::size(kIconSizes));
    CHECK((*images)[0].png);
    CHECK((*images)[0].width == 256);
    CHECK_FALSE(images->back().png);
    CHECK(images->back().width == 16);
    CHECK(images->back().bitCount == 32);
    // The top rows of the 32 px bitmap are margin (transparent); the middle is the picture.
    const auto& img32 = *std::ranges::find(*images, std::uint16_t{32}, &IconImage::width);
    const auto alphaAt = [&](int row) { // row from the top; DIB rows are stored bottom-up
        return static_cast<std::uint8_t>(img32.data[40 + static_cast<std::size_t>(31 - row) * 32 * 4 + 3]);
    };
    CHECK(alphaAt(0) == 0);
    CHECK(alphaAt(16) == 0xFF);

    auto ico = loadIconSource(file);
    REQUIRE(ico);
    const auto icoFile = scratch(L"picture.ico");
    REQUIRE(writeFileAtomic(icoFile, makeIco(*ico)));
    auto again = loadIconSource(icoFile); // an .ico is taken as it is
    REQUIRE(again);
    CHECK(again->size() == ico->size());

    if (std::filesystem::exists(kImageres)) {
        auto patched = patchIconBytes(bytesOf(kImageres), {{ResourceKey{3, {}}, *images}});
        REQUIRE(patched);
        const auto out = scratch(L"imageres.picture.mun");
        REQUIRE(writeFileAtomic(out, *patched));
        auto check = verifyIconFileWithWindows(out);
        REQUIRE(check);
        CHECK(check->failed == 0);
    }
    CHECK_FALSE(loadIconSource(scratch(L"missing.png")));
}

namespace {

// size × size BGRA: an opaque disc (blue-green) on transparency, top row first.
std::string discPixels(int size) {
    std::string pixels(static_cast<std::size_t>(size) * size * 4, '\0');
    const double r = size * 0.4;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double dx = x + 0.5 - size / 2.0;
            const double dy = y + 0.5 - size / 2.0;
            if (dx * dx + dy * dy <= r * r) {
                const std::size_t at = (static_cast<std::size_t>(y) * size + x) * 4;
                pixels[at] = static_cast<char>(0xC0);
                pixels[at + 1] = static_cast<char>(0x80);
                pixels[at + 3] = static_cast<char>(0xFF);
            }
        }
    }
    return pixels;
}

std::vector<int> widthsOf(const std::vector<IconImage>& images) {
    std::vector<int> out;
    for (const auto& i : images) {
        out.push_back(i.width);
    }
    return out;
}

// The alpha of a 32-bit DIB icon image's pixel (x, y from the top left).
std::uint8_t dibAlpha(const IconImage& img, int x, int y) {
    return static_cast<std::uint8_t>(img.data[40 + (static_cast<std::size_t>(img.height - 1 - y) * img.width + x) * 4 + 3]);
}

} // namespace

TEST_CASE("icons: a one-size icon is completed to Windows' sizes; what it had stays byte for byte (D-105)") {
    const IconImage only48 = dibIconImage(discPixels(48), 48);
    auto full = completeIconSizes({only48}, kIconSizes);
    REQUIRE(full);
    CHECK(widthsOf(*full) == std::vector<int>(std::begin(kIconSizes), std::end(kIconSizes))); // largest first
    CHECK((*full)[0].png);                                                                      // 256 as PNG, like Windows
    const auto& kept = *std::ranges::find(*full, std::uint16_t{48}, &IconImage::width);
    CHECK(kept.data == only48.data);
    const auto& made16 = full->back();
    CHECK_FALSE(made16.png);
    CHECK(made16.bitCount == 32);
    CHECK(dibAlpha(made16, 8, 8) == 0xFF); // the disc, scaled down
    CHECK(dibAlpha(made16, 0, 0) == 0);    // its transparent corner
    // Nothing missing: the images as given, in their order.
    std::vector<IconImage> reversed(full->rbegin(), full->rend());
    auto same = completeIconSizes(reversed, kIconSizes);
    REQUIRE(same);
    CHECK(widthsOf(*same) == widthsOf(reversed));
    CHECK_FALSE(completeIconSizes({}, kIconSizes));
    CHECK(iconSizesOf({only48, dibIconImage(discPixels(16), 16), only48}) == std::vector<int>{48, 16});
}

TEST_CASE("icons: shrinking is done premultiplied — no colour of transparent pixels at the edges") {
    // Left half: transparent but red underneath; right half: opaque blue.
    std::string pixels(64 * 64 * 4, '\0');
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * 64 + x) * 4;
            if (x < 32) {
                pixels[at + 2] = static_cast<char>(0xFF);
            } else {
                pixels[at] = static_cast<char>(0xFF);
                pixels[at + 3] = static_cast<char>(0xFF);
            }
        }
    }
    auto png = encodePngBgra(pixels, 64);
    REQUIRE(png);
    auto small = pictureBytesBgraSquare(*png, 16);
    REQUIRE(small);
    int edge = 0;
    for (std::size_t at = 0; at < small->size(); at += 4) {
        const auto alpha = static_cast<std::uint8_t>((*small)[at + 3]);
        if (alpha > 0 && alpha < 0xFF) {
            ++edge;
        }
        if (alpha > 0) {
            CHECK(static_cast<std::uint8_t>((*small)[at + 2]) < 16); // no red bled in
        }
    }
    CHECK(edge > 0); // there was an edge to look at
    CHECK_FALSE(pictureBytesBgraSquare("not a picture", 16));
}

TEST_CASE("icons: a replacement gets the sizes of the group it replaces, and Windows loads it") {
    if (!std::filesystem::exists(kImageres)) {
        return;
    }
    const std::string original = bytesOf(kImageres);
    auto pe = PeImage::parse(original);
    REQUIRE(pe);
    const auto groups = listIconGroups(pe->resources());
    // A group with a size outside kIconSizes (96 px and the like) and one with low-depth images.
    const auto odd = std::ranges::find_if(groups, [](const IconGroupInfo& g) {
        return std::ranges::any_of(iconSizesOf(g.images), [](int s) { return std::ranges::find(kIconSizes, s) == std::end(kIconSizes); });
    });
    const auto paletted = std::ranges::find_if(groups, [](const IconGroupInfo& g) {
        return std::ranges::any_of(g.images, [](const IconImage& i) { return !i.png && i.bitCount <= 8 && i.width >= 32; });
    });
    REQUIRE(odd != groups.end());
    REQUIRE(paletted != groups.end());
    const IconImage lowDepth = *std::ranges::find_if(paletted->images, [](const IconImage& i) { return !i.png && i.bitCount <= 8 && i.width >= 32; });

    const IconImage only32 = dibIconImage(discPixels(32), 32);
    std::vector<IconReplacement> replacements{{odd->key, {only32}}, {ResourceKey{3, {}}, {lowDepth}}};
    auto fitted = fitIconReplacements(*pe, replacements);
    REQUIRE(fitted);
    // The group's sizes plus what the replacement brought.
    const auto expected = [](std::vector<IconImage> group, const IconImage& given) {
        group.push_back(given);
        return iconSizesOf(group);
    };
    CHECK(iconSizesOf((*fitted)[0].images) == expected(odd->images, only32));
    CHECK(iconSizesOf((*fitted)[1].images) == expected(groupOf(groups, 3).images, lowDepth));
    CHECK(std::ranges::any_of((*fitted)[1].images, [&](const IconImage& i) { return i.data == lowDepth.data; }));

    auto patched = patchIconBytes(original, *fitted);
    REQUIRE(patched);
    const auto out = scratch(L"imageres.fitted.mun");
    REQUIRE(writeFileAtomic(out, *patched));
    auto check = verifyIconFileWithWindows(out);
    REQUIRE(check);
    CHECK(check->failed == 0);
    // A group the file does not have stays as given (patchIconBytes refuses it).
    auto unknown = fitIconReplacements(*pe, {{ResourceKey{65000, {}}, {only32}}});
    REQUIRE(unknown);
    CHECK((*unknown)[0].images.size() == 1);
}

TEST_CASE("icons: an icon copied into the image (the redirect mode) carries every size (D-105)") {
    const auto root = std::filesystem::temp_directory_path() / L"wl-tests" / L"icons" / L"redirect";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / L"ProgramData");
    const auto source = scratch(L"one-size.ico");
    REQUIRE(writeFileAtomic(source, makeIco({dibIconImage(discPixels(32), 32)})));
    CHECK(iconSourceLargest(source).value_or(0) == 32);

    REQUIRE(copyIconIntoImage(root, L"ProgramData\\WinLove\\Icons\\this-pc.ico", source));
    auto copied = parseIco(bytesOf(root / L"ProgramData" / L"WinLove" / L"Icons" / L"this-pc.ico"));
    REQUIRE(copied);
    CHECK(widthsOf(*copied) == std::vector<int>(std::begin(kIconSizes), std::end(kIconSizes)));

    CHECK_FALSE(copyIconIntoImage(root, L"Windows\\System32\\x.ico", source)); // copyImageFile's places only
    CHECK_FALSE(copyIconIntoImage(root, L"ProgramData\\WinLove\\Icons\\x.ico", scratch(L"missing.ico")));
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("icons: a queued patch is JSON with group keys and sources, or a restore") {
    IconPatchRequest request;
    request.groups = {{ResourceKey{3, {}}, LR"(C:\Icons\folder.ico)"}, {ResourceKey{0, L"ICO_MYCOMPUTER"}, LR"(D:\pc.png)"}};
    const auto value = iconPatchValue(request);
    auto back = iconPatchRequest(value);
    REQUIRE(back);
    CHECK_FALSE(back->restore);
    REQUIRE(back->groups.size() == 2);
    CHECK(back->groups[0].first == ResourceKey{3, {}});
    CHECK(back->groups[0].second == LR"(C:\Icons\folder.ico)");
    CHECK(back->groups[1].first.name == L"ICO_MYCOMPUTER");
    auto restore = iconPatchRequest(iconPatchValue(IconPatchRequest{true, {}}));
    REQUIRE(restore);
    CHECK(restore->restore);
    CHECK_FALSE(iconPatchRequest(L"{}"));
    CHECK_FALSE(iconPatchRequest(L"not json"));
    CHECK(resourceKeyFromText(L"#12") == ResourceKey{12, {}});
    CHECK(resourceKeyFromText(L"#99999").named());
    CHECK(resourceKeyFromText(L"BLANK").name == L"BLANK");
}
