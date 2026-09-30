// P11: an imported .reg file is queued whole, as writes that are also re-imported after setup.
#include "app/controllers/RegistryController.h"

#include <doctest.h>

#include <filesystem>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"reg-import";
    std::filesystem::create_directories(dir);
    return dir / name;
}

TweakCatalog emptyCatalog() {
    return *TweakCatalog::parse(R"({"format":"winlove.catalog.tweaks","categories":[],"tweaks":[]})");
}

} // namespace

TEST_CASE("imported .reg: every usable value is queued for offline + post-setup; the rest is counted") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    RegistryController controller{state, emptyCatalog()};

    auto writes = core::parseRegText(L"Windows Registry Editor Version 5.00\n"
                                     L"[HKEY_CURRENT_USER\\Software\\Contoso]\n\"Theme\"=dword:00000001\n"
                                     L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Contoso\\Empty]\n"
                                     L"[HKEY_CURRENT_CONFIG\\Software\\Fonts]\n\"LogPixels\"=dword:00000060\n"
                                     L"[HKEY_LOCAL_MACHINE\\SAM\\SAM]\n\"x\"=dword:00000001\n"
                                     L"[HKEY_USERS\\S-1-5-21-1-2-3-1001\\Software\\Contoso]\n\"y\"=\"z\"\n");
    REQUIRE(writes);
    controller.addImport(L"C:\\Tweaks\\contoso.reg", std::move(*writes));

    REQUIRE(state.regImports().size() == 1);
    const auto& import = state.regImports().front();
    CHECK(import.writes.size() == 3); // HKCU value, the empty HKLM key, the HKCC value (after setup only)
    CHECK(import.skipped == 2);       // SAM and another user's SID: nowhere to write them
    CHECK(state.changes().size() == 3);
    CHECK(state.changes().count(OpKind::SetRegistryFirstLogon) == 3);
    CHECK(state.changes().count(OpKind::SetRegistryValue) == 0);
    CHECK(state.changes().find(OpKind::SetRegistryFirstLogon, L"HKLM\\SOFTWARE\\Contoso\\Empty\\::") != nullptr);
    CHECK(controller.selection("custom") == std::pair{1, 1});
    CHECK(controller.checkedCount() == 1);

    controller.toggleImport(0);
    CHECK(state.changes().empty());
    CHECK(controller.selection("custom") == std::pair{0, 1});
    controller.toggleImport(0);
    CHECK(state.changes().size() == 3);
    controller.removeImport(0);
    CHECK(state.changes().empty());
    CHECK(state.regImports().empty());
}

TEST_CASE("imported .reg: delete-then-set keeps both, in file order; a repeated value keeps its last entry") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    RegistryController controller{state, emptyCatalog()};

    // The usual context-menu tweak: wipe the key, write it again; plus x set before and after.
    auto writes = core::parseRegText(L"Windows Registry Editor Version 5.00\n"
                                     L"[HKEY_CLASSES_ROOT\\Directory\\shell\\Tool]\n\"x\"=dword:00000001\n"
                                     L"[-HKEY_CLASSES_ROOT\\Directory\\shell\\Tool]\n"
                                     L"[HKEY_CLASSES_ROOT\\Directory\\shell\\Tool]\n@=\"Open tool\"\n\"x\"=dword:00000002\n");
    REQUIRE(writes);
    REQUIRE(writes->size() == 4);
    controller.addImport(L"C:\\Tweaks\\tool.reg", std::move(*writes));

    const auto& ops = state.changes().operations();
    REQUIRE(ops.size() == 3);
    CHECK(ops[0].value == L"[-]");
    CHECK(ops[1].target == L"HKCR\\Directory\\shell\\Tool::");
    CHECK(ops[1].value == L"\"Open tool\"");
    CHECK(ops[2].value == L"dword:00000002");
    CHECK(controller.selection("custom") == std::pair{1, 1}); // shown as checked

    // Importing a newer copy moves its slots behind what is queued, in its own order.
    state.queue(core::ops::Operation{OpKind::SetServiceStart, L"DiagTrack", L"disabled"});
    auto newer = core::parseRegText(L"REGEDIT4\n[-HKEY_CLASSES_ROOT\\Directory\\shell\\Tool]\n"
                                    L"[HKEY_CLASSES_ROOT\\Directory\\shell\\Tool]\n@=\"Open tool 2\"\n");
    REQUIRE(newer);
    controller.addImport(L"C:\\Tweaks\\tool2.reg", std::move(*newer));
    const auto& after = state.changes().operations();
    REQUIRE(after.size() == 4);
    CHECK(after[0].value == L"dword:00000002");
    CHECK(after[1].kind == OpKind::SetServiceStart);
    CHECK(after[2].value == L"[-]");
    CHECK(after[3].value == L"\"Open tool 2\"");
}
