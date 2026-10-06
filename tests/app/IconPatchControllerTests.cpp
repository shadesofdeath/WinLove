// D-068: Simgeler › Sistem simgeleri — the queue (one PatchIcons operation per file), pack folders
// in and out. This PC's C:\ stands in for the mounted image (only read).
#include "app/controllers/IconPatchController.h"
#include "base/File.h"
#include "core/image/icons/ResFile.h"
#include "core/system/Process.h"

#include <doctest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"icon-patch" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

const std::filesystem::path kBrand = std::filesystem::path(WL_SOURCE_DIR) / L"resources" / L"brand" / L"WinLove.ico";
const std::wstring kImageres = LR"(Windows\SystemResources\imageres.dll.mun)";

struct Fixture {
    std::filesystem::path dir;
    AppState state;
    IconPatchController controller{state, [](std::function<void()> f) { f(); }};
    explicit Fixture(const wchar_t* name = L"state") : dir(scratch(name)), state(dir / L"recent.json", dir / L"settings.json") {
        state.setMounted(MountedImage{L"C:\\", L"C:\\w\\install.wim", 1, L"Pro"});
    }
};

} // namespace

TEST_CASE("icon files: names in packs match the image's files and groups") {
    std::vector<IconPatchController::File> files{{LR"(Windows\SystemResources\imageres.dll.mun)", L"imageres.dll", 1},
                                                 {LR"(Windows\SystemResources\shell32.dll.mun)", L"shell32.dll", 1}};
    CHECK(IconPatchController::fileNamed(files, L"imageres.dll") == &files[0]);
    CHECK(IconPatchController::fileNamed(files, L"IMAGERES") == &files[0]);
    CHECK(IconPatchController::fileNamed(files, L"imageres.dll.mun") == &files[0]);
    CHECK(IconPatchController::fileNamed(files, L"shell32") == &files[1]);
    CHECK(IconPatchController::fileNamed(files, L"ddores.dll") == nullptr);
    CHECK(IconPatchController::keyFromPackName(L"3") == core::ResourceKey{3, {}});
    CHECK(IconPatchController::keyFromPackName(L"#109") == core::ResourceKey{109, {}});
    CHECK(IconPatchController::keyFromPackName(L"ICO_MYCOMPUTER").name == L"ICO_MYCOMPUTER");
    CHECK(IconPatchController::keyFromPackName(L"999999").named());
}

TEST_CASE("icon files: replacing, going back, restoring — one operation per file") {
    if (!std::filesystem::exists(L"C:\\Windows\\SystemResources\\imageres.dll.mun")) {
        return;
    }
    Fixture f;
    CHECK_FALSE(f.controller.files().empty());
    REQUIRE(f.controller.replace(kImageres, core::ResourceKey{3, {}}, kBrand));
    REQUIRE(f.controller.replace(kImageres, core::ResourceKey{109, {}}, kBrand));
    CHECK(f.state.changes().size() == 1);
    const auto* op = f.state.changes().find(OpKind::PatchIcons, kImageres);
    REQUIRE(op);
    CHECK(op->risk == core::ops::Risk::Medium);
    CHECK(f.controller.changedIn(kImageres) == 2);
    CHECK(f.controller.changedCount() == 2);
    CHECK(f.controller.replacement(kImageres, core::ResourceKey{3, {}}) == kBrand);

    // Not an icon: refused, nothing queued.
    const auto text = scratch(L"text") / L"notes.txt";
    REQUIRE(writeFileAtomic(text, "hello"));
    CHECK_FALSE(f.controller.replace(kImageres, core::ResourceKey{4, {}}, text));
    CHECK(f.controller.changedIn(kImageres) == 2);

    f.controller.revert(kImageres, core::ResourceKey{3, {}});
    CHECK(f.controller.changedIn(kImageres) == 1);
    f.controller.revert(kImageres, core::ResourceKey{109, {}});
    CHECK(f.state.changes().empty()); // the last one out takes the operation with it

    f.controller.setRestore(kImageres, true);
    CHECK(f.controller.restoreQueued(kImageres));
    CHECK(f.controller.changedCount() == 1);
    REQUIRE(f.controller.replace(kImageres, core::ResourceKey{3, {}}, kBrand)); // a new icon replaces the restore
    CHECK_FALSE(f.controller.restoreQueued(kImageres));
    f.controller.resetAll();
    CHECK(f.state.changes().empty());
}

TEST_CASE("icon files: a pack folder in, the queue out as a pack, and back in the same") {
    if (!std::filesystem::exists(L"C:\\Windows\\SystemResources\\shell32.dll.mun")) {
        return;
    }
    Fixture f;
    const auto pack = scratch(L"pack");
    std::filesystem::create_directories(pack / L"imageres.dll");
    std::filesystem::copy_file(kBrand, pack / L"imageres.dll" / L"3.ico");
    std::filesystem::copy_file(kBrand, pack / L"imageres.dll" / L"#109.ico");
    std::filesystem::copy_file(kBrand, pack / L"brand.ico");
    std::filesystem::create_directories(pack / L"nosuch.dll"); // a folder for a file the image lacks: ignored
    REQUIRE(writeFileAtomic(pack / L"iconpack.json",
                            R"({"name": "Test pack", "files": {"shell32": {"4": "brand.ico"},
                                "ddores_missing.dll": {"1": "brand.ico"}, "imageres": {"5": "../outside.ico"}}})"));
    auto result = f.controller.applyPack(pack);
    REQUIRE(result);
    CHECK(result->name == L"Test pack");
    CHECK(result->matched == 3);
    CHECK(result->unmatched.size() == 2); // the missing file, the path leaving the pack
    CHECK(f.controller.changedIn(kImageres) == 2);
    CHECK(f.controller.changedIn(LR"(Windows\SystemResources\shell32.dll.mun)") == 1);

    const auto out = scratch(L"exported");
    auto written = f.controller.exportPack(out);
    REQUIRE(written);
    CHECK(*written == 3);
    CHECK(std::filesystem::exists(out / L"imageres.dll" / L"3.ico"));
    CHECK(std::filesystem::exists(out / L"iconpack.json"));

    Fixture g(L"state2");
    auto again = g.controller.applyPack(out);
    REQUIRE(again);
    CHECK(again->matched == 3);
    CHECK(again->unmatched.empty());
    CHECK(g.controller.changedCount() == 3);
}

TEST_CASE("icon files: a 7TSP pack (folder or archive) is converted and queued; missing groups stay out") {
    if (!std::filesystem::exists(L"C:\\Windows\\SystemResources\\imageres.dll.mun")) {
        return;
    }
    Fixture f(L"state7tsp");
    const auto root = scratch(L"packroot");
    f.controller.setPackRoot(root);
    // imageres: group 3 (exists) and 60000 (no such group); Display.dll: a program's file, not patched.
    auto bytes = readFileBytes(kBrand);
    REQUIRE(bytes);
    auto images = core::parseIco(*bytes);
    REQUIRE(images);
    auto tree = [&](std::initializer_list<std::uint16_t> ids) {
        core::ResourceTree t;
        t.ensureType(core::kRtIcon);
        t.ensureType(core::kRtGroupIcon);
        std::uint16_t next = 1;
        for (const auto id : ids) {
            std::string dir("\0\0\1\0\1\0", 6);
            const auto& img = images->front();
            const std::uint16_t iconId = next++;
            core::ensureName(*t.type(core::kRtIcon), iconId).languages.push_back({0x409, 0, img.data});
            char e[14]{};
            e[0] = static_cast<char>(img.width >= 256 ? 0 : img.width);
            e[1] = static_cast<char>(img.height >= 256 ? 0 : img.height);
            std::memcpy(e + 4, &img.planes, 2);
            std::memcpy(e + 6, &img.bitCount, 2);
            const auto size = static_cast<std::uint32_t>(img.data.size());
            std::memcpy(e + 8, &size, 4);
            std::memcpy(e + 12, &iconId, 2);
            dir.append(e, 14);
            core::ensureName(*t.type(core::kRtGroupIcon), id).languages.push_back({0x409, 0, dir});
        }
        return core::writeResFile(t);
    };
    const auto pack = scratch(L"seventsp");
    std::filesystem::create_directories(pack / L"Resources");
    REQUIRE(writeFileAtomic(pack / L"Pack.ini", "[Base Pack]\nPack=Test 7TSP\n"));
    REQUIRE(writeFileAtomic(pack / L"Resources" / L"imageres.dll.mun.res", tree({3, 60000})));
    REQUIRE(writeFileAtomic(pack / L"Resources" / L"Display.dll.res", tree({1})));

    auto result = f.controller.importPack(pack);
    REQUIRE(result);
    CHECK(result->name == L"Test 7TSP");
    CHECK(result->matched == 1);
    CHECK(std::ranges::find(result->unmatched, L"imageres.dll.mun/#60000") != result->unmatched.end());
    CHECK(std::ranges::find(result->unmatched, L"Display.dll") != result->unmatched.end());
    CHECK(f.controller.changedIn(kImageres) == 1);
    const auto source = f.controller.replacement(kImageres, core::ResourceKey{3, {}});
    REQUIRE(source);
    CHECK(source->wstring().starts_with((root / L"Test 7TSP").wstring())); // kept under the pack root
    CHECK(std::filesystem::exists(*source));

    // The same pack zipped: opened, converted, queued the same; nothing extracted is left behind.
    const auto tar = core::systemTool(L"tar.exe");
    if (!tar || !std::filesystem::exists(*tar)) {
        return;
    }
    const auto zip = scratch(L"zipped") / L"Test pack.zip";
    auto made = core::runProcess(L"\"" + *tar + L"\" -a -c -f \"" + zip.wstring() + L"\" -C \"" + pack.wstring() + L"\" Pack.ini Resources", {});
    REQUIRE(made);
    REQUIRE(*made == 0);
    Fixture g(L"state7tspzip");
    g.controller.setPackRoot(root);
    auto zipped = g.controller.importPack(zip);
    REQUIRE(zipped);
    CHECK(zipped->matched == 1);
    CHECK(g.controller.changedIn(kImageres) == 1);
    CHECK_FALSE(std::filesystem::exists(root / L"~Test pack"));
}
