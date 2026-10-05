// P11 (D-067): the user's own entries — an imported .reg file is queued whole, as writes that are
// also re-imported after setup; a typed value is one write, re-applied after setup or offline only.
#include "app/controllers/RegistryController.h"
#include "core/image/RegistryInput.h"

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

} // namespace

TEST_CASE("imported .reg: every usable value is queued for offline + post-setup; the rest is counted") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    RegistryController controller{state};

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
    CHECK(controller.selection() == std::pair{1, 1});
    CHECK(controller.checkedCount() == 1);

    controller.toggle(0);
    CHECK(state.changes().empty());
    CHECK(controller.selection() == std::pair{0, 1});
    controller.toggle(0);
    CHECK(state.changes().size() == 3);
    controller.remove(0);
    CHECK(state.changes().empty());
    CHECK(state.regImports().empty());
}

TEST_CASE("imported .reg: delete-then-set keeps both, in file order; a repeated value keeps its last entry") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    RegistryController controller{state};

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
    CHECK(controller.selection() == std::pair{1, 1}); // shown as checked

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

TEST_CASE("typed values: queued with the chosen kind, edited in place, switched off and removed") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    RegistryController controller{state};

    auto policy = core::registryWriteFromInput(LR"(HKEY_LOCAL_MACHINE\SOFTWARE\Contoso)", L"Mode", core::RegValueType::Dword, L"0x10");
    REQUIRE(policy);
    REQUIRE(controller.addValue(*policy, /*afterSetup=*/false));
    auto user = core::registryWriteFromInput(LR"(HKCU\Software\Contoso)", L"", core::RegValueType::String, L"hello");
    REQUIRE(user);
    REQUIRE(controller.addValue(*user, /*afterSetup=*/true));

    REQUIRE(state.regImports().size() == 2);
    CHECK(state.regImports()[0].typed);
    const auto* mode = state.changes().find(OpKind::SetRegistryValue, LR"(HKLM\SOFTWARE\Contoso::Mode)");
    REQUIRE(mode);
    CHECK(mode->value == L"dword:00000010");
    const auto* def = state.changes().find(OpKind::SetRegistryFirstLogon, LR"(HKCU\Software\Contoso::)");
    REQUIRE(def);
    CHECK(def->value == L"\"hello\"");
    CHECK(controller.selection() == std::pair{2, 2});

    // Edit: the new value (and kind) replaces the old one in the queue.
    auto edited = core::registryWriteFromInput(LR"(HKLM\SOFTWARE\Contoso)", L"Mode", core::RegValueType::Dword, L"7");
    REQUIRE(edited);
    REQUIRE(controller.replaceValue(0, *edited, /*afterSetup=*/true));
    CHECK_FALSE(state.changes().find(OpKind::SetRegistryValue, LR"(HKLM\SOFTWARE\Contoso::Mode)"));
    REQUIRE(state.changes().find(OpKind::SetRegistryFirstLogon, LR"(HKLM\SOFTWARE\Contoso::Mode)"));
    CHECK(state.changes().find(OpKind::SetRegistryFirstLogon, LR"(HKLM\SOFTWARE\Contoso::Mode)")->value == L"dword:00000007");
    CHECK(state.regImports()[0].afterSetup);

    // Off: out of the queue, still listed; an edit while off keeps it off.
    controller.toggle(1);
    CHECK_FALSE(controller.checked(1));
    CHECK(state.changes().size() == 1);
    auto other = core::registryWriteFromInput(LR"(HKCU\Software\Contoso)", L"", core::RegValueType::String, L"bye");
    REQUIRE(controller.replaceValue(1, *other, true));
    CHECK_FALSE(controller.checked(1));
    CHECK(state.changes().size() == 1);
    CHECK(controller.checkedCount() == 1);

    controller.remove(0);
    CHECK(state.changes().empty());
    CHECK(state.regImports().size() == 1);

    // A key no image has: refused.
    auto sam = core::registryWriteFromInput(LR"(HKLM\SAM\SAM)", L"x", core::RegValueType::Dword, L"1");
    REQUIRE(sam);
    CHECK_FALSE(controller.addValue(*sam, true));
    CHECK(state.regImports().size() == 1);
}

TEST_CASE("typed values: two values of one key are separate entries; a .reg file re-imported replaces its old copy") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    RegistryController controller{state};
    controller.addValue(*core::registryWriteFromInput(LR"(HKCU\Software\A)", L"x", core::RegValueType::Dword, L"1"), true);
    controller.addValue(*core::registryWriteFromInput(LR"(HKCU\Software\A)", L"y", core::RegValueType::Dword, L"2"), true);
    CHECK(state.regImports().size() == 2);

    controller.addImport(L"C:\\t.reg", *core::parseRegText(L"REGEDIT4\n[HKEY_CURRENT_USER\\Software\\B]\n\"old\"=dword:00000001\n"));
    controller.addImport(L"C:\\t.reg", *core::parseRegText(L"REGEDIT4\n[HKEY_CURRENT_USER\\Software\\B]\n\"new\"=dword:00000001\n"));
    CHECK(state.regImports().size() == 3);
    CHECK_FALSE(state.changes().find(OpKind::SetRegistryFirstLogon, LR"(HKCU\Software\B::old)"));
    CHECK(state.changes().find(OpKind::SetRegistryFirstLogon, LR"(HKCU\Software\B::new)"));
}
