// D-125: the update guard — what a run's steps ask to keep, and the files Apply leaves in the image.
#include "base/Utf8.h"
#include "core/image/SystemComponents.h"
#include "core/ops/UpdateGuard.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::core;
using namespace wl::core::ops;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

Operation op(OpKind kind, std::wstring target, std::wstring value = {}) {
    return Operation{kind, std::move(target), std::move(value)};
}

} // namespace

TEST_CASE("update guard: services, machine values, apps, tasks and component files from the steps that ran") {
    ComponentRecipe recipe;
    recipe.title = L"OneDrive";
    recipe.paths = {L"Windows\\System32\\OneDriveSetup.exe"};
    recipe.appx = {L"Microsoft.OneDriveSync"};
    const std::vector<Operation> ran{
        op(OpKind::SetServiceStart, L"DiagTrack", L"disabled"),
        op(OpKind::SetServiceStart, L"WSearch", L"autoDelayed"),
        op(OpKind::SetServiceStart, L"DiagTrack", L"manual"), // a later step wins
        op(OpKind::SetServiceStart, L"disk", L"boot"),        // a driver: not kept
        op(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection::AllowTelemetry", L"dword:00000000"),
        op(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Edge::HomepageLocation", L"\"https://example.org\""),
        op(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot::TurnOffWindowsCopilot", L"-"),
        op(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Big::Q", L"hex(b):01,00,00,00,00,00,00,00"),
        op(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Big::Bin", L"hex:0a,ff"),
        op(OpKind::SetRegistryValue, L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Search::SearchboxTaskbarMode", L"dword:00000000"),
        op(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce::Once", L"\"cmd /c x\""),
        op(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon::AutoAdminLogon", L"\"1\""),
        op(OpKind::RemoveAppx, L"Microsoft.BingNews_4.55.0.0_x64__8wekyb3d8bbwe", L"Haberler"),
        op(OpKind::RemoveAppx, L"Microsoft.BingWeather", L"Hava Durumu"),
        op(OpKind::SetTaskState, L"\\Microsoft\\Windows\\Application Experience\\ProgramDataUpdater", L"disabled"),
        op(OpKind::SetTaskState, L"\\Microsoft\\Windows\\Feedback\\Siuf\\DmClient", L"disabled"),
        op(OpKind::SetTaskState, L"\\Microsoft\\Windows\\Feedback\\Siuf\\DmClient", L"enabled"), // switched back on
        op(OpKind::RemoveComponent, L"onedrive", utf8::toWide(componentRecipeToJson(recipe))),
        op(OpKind::EnableFeature, L"Microsoft-Windows-Subsystem-Linux"), // nothing to keep
    };
    const UpdateGuardPlan plan = updateGuardPlan(ran);

    REQUIRE(plan.services.size() == 2);
    CHECK(plan.services[0] == UpdateGuardService{L"DiagTrack", 3, false});
    CHECK(plan.services[1] == UpdateGuardService{L"WSearch", 2, true});

    REQUIRE(plan.values.size() == 5); // per-user, RunOnce and Winlogon values are not kept
    CHECK(plan.values[0] == UpdateGuardValue{L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", L"dword", L"0"});
    CHECK(plan.values[1] == UpdateGuardValue{L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Edge", L"HomepageLocation", L"sz", L"https://example.org"});
    CHECK(plan.values[2] == UpdateGuardValue{L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", L"delete", L""});
    CHECK(plan.values[3] == UpdateGuardValue{L"HKLM\\SOFTWARE\\Big", L"Q", L"qword", L"1"});
    CHECK(plan.values[4] == UpdateGuardValue{L"HKLM\\SOFTWARE\\Big", L"Bin", L"binary", L"0aff"});

    REQUIRE(plan.apps.size() == 3);
    CHECK(plan.apps[0] == UpdateGuardApp{L"Microsoft.BingNews", L"Microsoft.BingNews_8wekyb3d8bbwe"});
    CHECK(plan.apps[1] == UpdateGuardApp{L"Microsoft.BingWeather", L""});
    CHECK(plan.apps[2] == UpdateGuardApp{L"Microsoft.OneDriveSync", L""});

    CHECK(plan.tasks == std::vector<std::wstring>{L"\\Microsoft\\Windows\\Application Experience\\ProgramDataUpdater"});
    REQUIRE(plan.components.size() == 1);
    CHECK(plan.components[0].title == L"OneDrive");
    CHECK(plan.components[0].paths == recipe.paths);

    // The file the script reads, and back.
    const auto again = updateGuardFromJson(updateGuardToJson(plan));
    REQUIRE(again);
    CHECK(again->services == plan.services);
    CHECK(again->values == plan.values);
    CHECK(again->apps == plan.apps);
    CHECK(again->tasks == plan.tasks);
    CHECK(again->components == plan.components);
    CHECK_FALSE(again->reportOnly);
    CHECK_FALSE(updateGuardFromJson("{\"format\":\"something else\"}"));
}

TEST_CASE("update guard: written into the image with the SetupComplete line that registers its task") {
    const auto root = std::filesystem::temp_directory_path() / L"wl-tests" / L"updateguard";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto mount = root / L"mount";
    std::filesystem::create_directories(mount / L"Windows", ec);
    std::filesystem::create_directories(mount / L"ProgramData", ec);

    REQUIRE(writeUpdateGuard(mount, UpdateGuardPlan{})); // nothing to keep: nothing written
    CHECK_FALSE(std::filesystem::exists(mount / L"ProgramData" / L"WinLove"));

    const UpdateGuardPlan plan = updateGuardPlan({op(OpKind::SetServiceStart, L"DiagTrack", L"disabled")});
    REQUIRE(writeUpdateGuard(mount, plan));
    const auto folder = mount / L"ProgramData" / L"WinLove" / L"Guard";
    const std::string script = readFile(folder / L"updateguard.ps1");
    CHECK(script.find("param([switch] $Register, [switch] $Remove)") != std::string::npos);
    CHECK(script.find("Microsoft-Windows-WindowsUpdateClient") != std::string::npos);
    const auto json = updateGuardFromJson(readFile(folder / L"guard.json"));
    REQUIRE(json);
    CHECK(json->services == plan.services);
    const std::string hook = readFile(mount / L"Windows" / L"Setup" / L"Scripts" / L"WinLove" / L"updateguard.cmd");
    CHECK(hook.find("updateguard.ps1\" -Register") != std::string::npos);
    const std::string complete = readFile(mount / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd");
    CHECK(complete.find("WinLove\\updateguard.cmd") != std::string::npos);
}
