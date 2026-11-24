// The changeset of the D-153 … D-155 install test in a VM (tools\lab_vm.ps1): built by the app's own
// code — the policy and menu controllers' operations, and the removal gaps the queue adds itself —
// from the image's ADMX files and SOFTWARE hive (copies). Skipped in the normal run:
//   set WL_LAB_IMAGE=<folder with Windows\PolicyDefinitions and Windows\System32\config\SOFTWARE>
//   set WL_LAB_OUT=<changeset.json>   [set WL_LAB_CHECK=tools\vm_check_policies_menu.ps1]
//   wl_tests.exe --test-case="lab: *" --no-skip
// then: tools\lab_vm.ps1 -Changes <changeset.json> -LogDisk -ShutdownAfter 90
//   -FirstLogon "powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\ProgramData\WinLoveLab\check.ps1"
// (2026-11-24: 49 steps, the guest's 38 checks passed — STATUS).
#include "app/catalog/ImageSettingsCatalog.h"
#include "app/controllers/ContextMenuController.h"
#include "app/controllers/PolicyController.h"
#include "app/controllers/RemovalGaps.h"
#include "base/Utf8.h"
#include "core/image/ContextMenu.h"
#include "core/policy/Admx.h"
#include "core/policy/PolicyWrites.h"

#include <doctest.h>
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <tuple>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;
using core::policy::PolicyState;

namespace {

std::string readText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path env(const wchar_t* name) {
    wchar_t value[1024]{};
    const DWORD n = GetEnvironmentVariableW(name, value, static_cast<DWORD>(std::size(value)));
    return n > 0 && n < std::size(value) ? std::filesystem::path(value) : std::filesystem::path();
}

} // namespace

TEST_CASE("lab: the VM install test's changeset (policies, context menu, removal gaps)" * doctest::skip()) {
    const auto image = env(L"WL_LAB_IMAGE");
    const auto out = env(L"WL_LAB_OUT");
    REQUIRE(!image.empty());
    REQUIRE(!out.empty());

    // 3 · Removal gaps: the three apps removed through the queue, the companion adds the fixes. The
    // Notepad fix needs the image's classic notepad.exe (26200 has it); a stand-in folder says so.
    const auto scratch = std::filesystem::temp_directory_path() / L"wl-tests" / L"lab-changeset";
    std::filesystem::create_directories(scratch / L"mount" / L"Windows" / L"System32");
    std::ofstream(scratch / L"mount" / L"Windows" / L"System32" / L"notepad.exe") << "stand-in";
    AppState state{scratch / L"recent.json", scratch / L"settings.json"};
    const auto settings = ImageSettingsCatalog::parse(readText(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/settings.json"));
    REQUIRE(settings);
    RemovalGaps gaps{*settings, [&] { return scratch / L"mount"; }};
    state.setMounted(MountedImage{scratch / L"mount", L"C:\\lab\\install.wim", 4, L"Pro"});
    state.setQueueCompanion(&gaps);
    for (const wchar_t* app : {L"Microsoft.ScreenSketch_11.2307.61.0_x64__8wekyb3d8bbwe",
                               L"Microsoft.Windows.Photos_24.24070.23002.0_x64__8wekyb3d8bbwe",
                               L"Microsoft.WindowsNotepad_11.2501.31.0_x64__8wekyb3d8bbwe"}) {
        state.queue(Operation{OpKind::RemoveAppx, app});
    }
    state.setQueueCompanion(nullptr);
    core::ops::ChangeSet changes = state.changes().withoutHistory();
    CHECK(changes.count(OpKind::RemoveAppx) == 3);
    CHECK(changes.find(OpKind::SetDefaultApps, L"associations") != nullptr);

    // 4 · Group Policy: machine Enabled with a chosen option, machine Enabled with defaults, machine
    // Disabled (an explicit disabled value), a user policy.
    auto catalog = core::policy::readPolicyDefinitions(image / L"Windows" / L"PolicyDefinitions", L"tr-TR");
    REQUIRE(catalog);
    auto policy = [&](const wchar_t* id, bool user, PolicyState chosenState, core::policy::PolicyValues values) {
        const auto* p = catalog->policy(id);
        REQUIRE_MESSAGE(p != nullptr, utf8::fromWide(id));
        auto chosen = core::policy::defaultValues(*p);
        for (auto& [element, value] : values) {
            chosen[element] = value;
        }
        auto op = PolicyController::operationFor(*p, user, {chosenState, std::move(chosen)});
        REQUIRE(op);
        changes.add(std::move(*op));
    };
    policy(L"Microsoft.Policies.WindowsUpdate:AutoUpdateCfg", false, PolicyState::Enabled, {{L"AutoUpdateMode", L"0"}});
    policy(L"Microsoft.Policies.DataCollection:AllowTelemetry", false, PolicyState::Enabled, {});
    policy(L"Microsoft.Policies.TerminalServer:TS_DISABLE_CONNECTIONS", false, PolicyState::Disabled, {});
    policy(L"Microsoft.Policies.WindowsExplorer:DisableSearchBoxSuggestions", true, PolicyState::Enabled, {});
    CHECK(changes.count(OpKind::SetPolicy) == 4);

    // 5 · Context menu: a verb, two handlers and a Windows 11 app item hidden, a verb the image hides
    // shown, two own items (a file one, a background one: %1 → %V).
    auto entries = core::readContextMenu(image);
    REQUIRE(entries);
    auto entry = [&](const wchar_t* id) {
        const auto it = std::ranges::find_if(*entries, [&](const core::ContextMenuEntry& e) { return e.id == id; });
        REQUIRE_MESSAGE(it != entries->end(), utf8::fromWide(id));
        return *it;
    };
    for (const wchar_t* id : {L"*\\shell\\pintohomefile", L"{E2BF9676-5F8F-435C-97EB-11607A5BEDF7}",
                              L"{596AB062-B4D2-4215-9F74-E9109B0A8153}"}) {
        auto e = entry(id);
        e.hidden = false; // the hive copy carries an earlier lab's own edits; the image shows these
        auto op = ContextMenuController::operationFor(e, true);
        CHECK(op.value.find(L"\"writes\"") != std::wstring::npos);
        changes.add(std::move(op));
    }
    {
        // The packaged items come from WindowsApps, which the hive copy has not: Terminal's, as
        // readPackagedContextMenu reads it from the mounted image (lab 2, D-155).
        core::ContextMenuEntry terminal;
        terminal.kind = core::ContextMenuEntry::Kind::Packaged;
        terminal.id = terminal.clsid = L"{9F156763-7844-4DC4-B2B1-901F640F5155}";
        terminal.keyName = L"OpenTerminalHere";
        terminal.name = L"OpenTerminalHere (WindowsTerminal)";
        terminal.places = {L"Directory", L"Directory\\Background"};
        terminal.source = L"Microsoft.WindowsTerminal";
        changes.add(ContextMenuController::operationFor(terminal, true));
    }
    {
        auto explore = entry(L"Folder\\shell\\explore");
        CHECK(explore.hidden);
        changes.add(ContextMenuController::operationFor(explore, false));
    }
    for (auto [name, command, place] : {std::tuple{L"Not Defteri ile aç", L"notepad.exe \"%1\"", L"*"},
                                        std::tuple{L"Komut istemi burada", L"cmd.exe /k cd /d \"%1\"", L"Directory\\Background"}}) {
        core::ContextMenuItem item;
        item.name = name;
        item.command = command;
        item.id = core::contextMenuItemId(name);
        item.places = {place};
        auto op = ContextMenuController::operationFor(item);
        REQUIRE(op);
        changes.add(std::move(*op));
    }
    CHECK(changes.count(OpKind::SetContextMenu) == 7);

    // The guest's check, run at the first sign-in.
    if (const auto check = env(L"WL_LAB_CHECK"); !check.empty()) {
        Operation op{OpKind::WriteFile, L"ProgramData\\WinLoveLab\\check.ps1", utf8::toWide(readText(check))};
        changes.add(std::move(op));
    }
    std::ofstream(out, std::ios::binary) << changes.toJson();
    MESSAGE(changes.operations().size() << " operations -> " << out.string());
}
