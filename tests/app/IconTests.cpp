// D-065: icon slots, packs matched by name or manifest, and what a slot queues.
#include "app/catalog/IconCatalog.h"
#include "app/controllers/IconController.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"icons" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

// A minimal valid .ico (one 16×16 entry header + a little payload).
void writeIcon(const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::binary);
    const unsigned char header[] = {0, 0, 1, 0, 1, 0, 16, 16, 0, 0, 1, 0, 32, 0, 40, 0, 0, 0, 22, 0, 0, 0};
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    const std::string payload(40, '\0');
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
}

} // namespace

TEST_CASE("icons: slots, names a pack is matched by, icon files") {
    CHECK(iconSlots().size() == 14);
    CHECK(findIconSlot("this-pc"));
    CHECK_FALSE(findIconSlot("nope"));
    CHECK(iconAliasKey(L"This PC") == L"thispc");
    CHECK(iconAliasKey(L"Geri Dönüşüm_Kutusu") == L"geridonusumkutusu");

    const auto dir = scratch(L"pack");
    writeIcon(dir / L"Computer.ico");
    writeIcon(dir / L"recycle-bin-full.ico");
    writeIcon(dir / L"Folder.ico");
    writeIcon(dir / L"something-else.ico");
    std::ofstream(dir / L"not-an-icon.ico") << "PNG";
    std::filesystem::create_directories(dir / L"drives");
    writeIcon(dir / L"drives" / L"usb.ico");
    CHECK(isIconFile(dir / L"Computer.ico"));
    CHECK_FALSE(isIconFile(dir / L"not-an-icon.ico"));

    auto pack = readIconPack(dir);
    REQUIRE(pack);
    CHECK(pack->name == L"pack");
    CHECK(pack->icons.at("this-pc").filename() == L"Computer.ico");
    CHECK(pack->icons.at("recycle-full").filename() == L"recycle-bin-full.ico");
    CHECK(pack->icons.at("folder").filename() == L"Folder.ico");
    CHECK(pack->icons.at("removable").filename() == L"usb.ico");
    CHECK(pack->unmatched == std::vector<std::wstring>{L"something-else.ico"});

    // A manifest names files for slots; paths outside the pack are ignored.
    std::ofstream(dir / L"iconpack.json")
        << R"({"name":"Fluent Dark","author":"someone","icons":{"network":"something-else.ico","drive":"..\\..\\x.ico"}})";
    pack = readIconPack(dir);
    REQUIRE(pack);
    CHECK(pack->name == L"Fluent Dark");
    CHECK(pack->icons.at("network").filename() == L"something-else.ico");
    CHECK_FALSE(pack->icons.contains("drive"));
    CHECK(pack->unmatched.empty());
    std::ofstream(dir / L"iconpack.json") << "{nope";
    CHECK_FALSE(readIconPack(dir));
}

TEST_CASE("icons: what a slot queues, resetting, the shortcut arrow") {
    const auto dir = scratch(L"queue");
    AppState state{dir / L"recent.json", dir / L"settings.json"};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    IconController icons(state);
    writeIcon(dir / L"pc.ico");

    const auto& pc = *findIconSlot("this-pc");
    REQUIRE(icons.assign(pc, dir / L"pc.ico"));
    CHECK(icons.assigned(pc) == dir / L"pc.ico");
    const auto& changes = state.changes();
    CHECK(changes.find(OpKind::CopyFile, L"ProgramData\\WinLove\\Icons\\this-pc.ico"));
    const auto* machine = changes.find(OpKind::SetRegistryValue,
                                       L"HKLM\\SOFTWARE\\Classes\\CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\DefaultIcon::");
    REQUIRE(machine);
    const auto write = core::registryWriteFrom(machine->target, machine->value);
    REQUIRE(write);
    CHECK(write->type == REG_EXPAND_SZ);
    CHECK(changes.find(OpKind::SetRegistryFirstLogon,
                       L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\DefaultIcon::"));
    CHECK(changes.find(OpKind::SetRegistryFirstLogon, IconController::themeKeepsIcons().target));
    CHECK(icons.changedCount() == 1);
    CHECK_FALSE(icons.assign(pc, dir / L"missing.ico"));

    // The empty bin also sets the default value; a shell icon is one value.
    const auto empty = IconController::operationsFor(*findIconSlot("recycle-empty"), dir / L"pc.ico");
    CHECK(std::ranges::count_if(empty, [](const auto& op) { return op.target.ends_with(L"DefaultIcon::"); }) == 2);
    const auto folder = IconController::operationsFor(*findIconSlot("folder"), dir / L"pc.ico");
    REQUIRE(folder.size() == 2);
    CHECK(folder[1].target == L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons::3");

    REQUIRE(icons.setShortcutArrowRemoved(true));
    CHECK(icons.shortcutArrowRemoved());
    const auto arrow = icons.assigned(*findIconSlot("shortcut"));
    REQUIRE(arrow);
    CHECK(isIconFile(*arrow)); // the transparent icon written into the work folder
    REQUIRE(icons.setShortcutArrowRemoved(false));
    CHECK_FALSE(icons.shortcutArrowRemoved());

    icons.reset(pc);
    CHECK(state.changes().empty()); // the theme value went with the last desktop icon
}
