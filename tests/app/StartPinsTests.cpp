// D-069: Başlat menüsü — the image's apps, the pin list JSON, the queue (modes, order, applyOnce).
// This PC's C:\ stands in for the mounted image (only read).
#include "app/controllers/StartPinsController.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/ImageFiles.h"
#include "core/image/RegistryEdit.h"

#include <doctest.h>
#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace wl;
using namespace wl::app;
using core::StartApp;
using core::ops::OpKind;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"start-pins" / name;
    std::filesystem::create_directories(dir);
    return dir;
}

struct Fixture {
    std::filesystem::path dir = scratch(L"state");
    AppState state{dir / L"recent.json", dir / L"settings.json"};
    StartPinsController controller{state, [](std::function<void()> f) { f(); }};
    Fixture() { state.setMounted(MountedImage{L"C:\\", L"C:\\w\\install.wim", 1, L"Pro"}); }
};

const StartApp kTerminal{StartApp::Kind::Packaged, L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", L"Terminal", {}};
const StartApp kExplorer{StartApp::Kind::DesktopLink, LR"(%APPDATA%\Microsoft\Windows\Start Menu\Programs\File Explorer.lnk)",
                         L"File Explorer", {}};
const StartApp kEdge{StartApp::Kind::DesktopId, L"MSEdge", L"Microsoft Edge", {}};

} // namespace

TEST_CASE("start apps: package folder names, readable names") {
    CHECK(core::familyFromFullName(L"Microsoft.WindowsCalculator_11.2405.2.0_x64__8wekyb3d8bbwe") ==
          L"Microsoft.WindowsCalculator_8wekyb3d8bbwe");
    CHECK(core::familyFromFullName(L"Microsoft.WindowsCalculator_11.2405.2.0_neutral_split.scale-100_8wekyb3d8bbwe").empty());
    CHECK(core::familyFromFullName(L"Microsoft.Foo_1.0.0.0_neutral_~_8wekyb3d8bbwe").empty()); // a bundle folder
    CHECK(core::familyFromFullName(L"NotAPackage").empty());
    CHECK(core::readableAppName(L"Microsoft.WindowsCalculator") == L"Calculator");
    CHECK(core::readableAppName(L"Microsoft.ZuneMusic") == L"Media Player");
    CHECK(core::readableAppName(L"Contoso.GreatPhotoEditor") == L"Great Photo Editor");
}

TEST_CASE("start apps: this PC's Windows lists packaged apps, Settings and shortcuts") {
    if (!std::filesystem::exists(L"C:\\Windows\\ImmersiveControlPanel")) {
        return;
    }
    const auto apps = core::listStartApps(L"C:\\");
    CHECK(apps.size() > 10);
    auto has = [&](std::wstring_view id) { return std::ranges::any_of(apps, [&](const StartApp& a) { return a.id == id; }); };
    CHECK(has(L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel"));
    CHECK(has(LR"(%APPDATA%\Microsoft\Windows\Start Menu\Programs\File Explorer.lnk)"));
    // No framework, no resource package, no app hidden from the list.
    CHECK_FALSE(std::ranges::any_of(apps, [](const StartApp& a) { return a.id.find(L"VCLibs") != std::wstring::npos; }));
    // Sorted by name, case-insensitively.
    for (std::size_t i = 1; i < apps.size(); ++i) {
        CHECK(text::lower(apps[i - 1].name) <= text::lower(apps[i].name));
    }
}

TEST_CASE("start pins: Microsoft's JSON, both policy forms and the file, read back from the queue") {
    core::StartPinsPlan plan{true, {kExplorer, kTerminal, kEdge}, true};
    const auto json = nlohmann::json::parse(core::startPinsJson(plan));
    CHECK(json["applyOnce"] == true);
    REQUIRE(json["pinnedList"].size() == 3);
    CHECK(json["pinnedList"][0]["desktopAppLink"] == R"(%APPDATA%\Microsoft\Windows\Start Menu\Programs\File Explorer.lnk)");
    CHECK(json["pinnedList"][1]["packagedAppId"] == "Microsoft.WindowsTerminal_8wekyb3d8bbwe!App");
    CHECK(json["pinnedList"][2]["desktopAppId"] == "MSEdge");

    const auto ops = core::startPinsOperations(plan);
    REQUIRE(ops.size() == 5); // custom: the empty state under the list, no Windows 10 tile file
    for (const auto& op : ops) {
        if (op.kind == OpKind::SetRegistryValue) {
            CHECK(core::registryWriteFrom(op.target, op.value).has_value()); // the Applier can write it
        }
    }
    const auto policy = core::registryWriteFrom(ops[0].target, ops[0].value);
    REQUIRE(policy);
    CHECK(policy->type == REG_SZ);
    const std::wstring stored(reinterpret_cast<const wchar_t*>(policy->data.data()), policy->data.size() / 2 - 1);
    CHECK(nlohmann::json::parse(utf8::fromWide(stored)) == json);
    const auto path = core::registryWriteFrom(ops[2].target, ops[2].value);
    REQUIRE(path);
    CHECK(path->type == REG_EXPAND_SZ);

    const auto back = core::startPinsPlanFromOperations(ops);
    REQUIRE(back);
    CHECK(back->custom);
    CHECK(back->applyOnce);
    CHECK(back->pins == plan.pins);

    const auto empty = core::startPinsOperations(core::StartPinsPlan{false, {}, false});
    CHECK(empty.size() == 6); // + the empty Windows 10 layout and the empty Start state (every edition)
    // The empty Start state goes in as bytes: Win11Debloat's 972-byte file, byte for byte.
    const auto state = std::ranges::find_if(empty, [](const auto& op) { return op.target.ends_with(L"start2.bin"); });
    REQUIRE(state != empty.end());
    const std::string bytes = core::imageFileBytes(state->value);
    CHECK(bytes.size() == 972);
    CHECK(static_cast<unsigned char>(bytes[0]) == 0xE2);
    CHECK(static_cast<unsigned char>(bytes[1]) == 0x7A);
    CHECK(core::validateImageFile(state->target, bytes.size()));
    const auto source = std::filesystem::path(WL_SOURCE_DIR) / L"third_party" / L"win11debloat" / L"start2-empty.bin";
    if (std::filesystem::exists(source)) {
        std::ifstream in(source, std::ios::binary);
        const std::string file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(file == bytes);
    }
    const auto none = core::startPinsPlanFromOperations(empty);
    REQUIRE(none);
    CHECK_FALSE(none->custom);
    CHECK_FALSE(none->applyOnce);
    CHECK_FALSE(core::startPinsPlanFromOperations({}));
}

TEST_CASE("start pins: which builds apply a custom list (measured in VMs)") {
    CHECK_FALSE(core::startAppliesCustomPins(26200, 8037));
    CHECK(core::startAppliesCustomPins(26200, 9457));
    CHECK(core::startAppliesCustomPins(26100, 9600));
    CHECK_FALSE(core::startAppliesCustomPins(22631, 5000)); // 23H2 never got the change
    CHECK(core::startAppliesCustomPins(27000, 1));
}

TEST_CASE("start pins: modes, order and applyOnce go through the queue; a preset keeps them") {
    Fixture f;
    CHECK(f.controller.mode() == StartPinsController::Mode::Windows);
    CHECK(f.controller.changedCount() == 0);

    f.controller.setMode(StartPinsController::Mode::Empty);
    CHECK(f.controller.mode() == StartPinsController::Mode::Empty);
    CHECK(f.state.changes().size() == 6);

    f.controller.setMode(StartPinsController::Mode::Custom);
    CHECK(f.controller.mode() == StartPinsController::Mode::Custom); // still no pins
    f.controller.add(kTerminal);
    f.controller.add(kExplorer);
    f.controller.add(kTerminal); // already there
    f.controller.add(kEdge);
    REQUIRE(f.controller.pins().size() == 3);
    CHECK(f.state.changes().size() == 5); // the tile file went with "Empty"
    f.controller.move(2, -1);             // Edge before File Explorer
    CHECK(f.controller.pins()[1] == kEdge);
    f.controller.move(0, -1); // nowhere to go
    CHECK(f.controller.pins()[0] == kTerminal);
    f.controller.remove(0);
    REQUIRE(f.controller.pins().size() == 2);
    CHECK(f.controller.pins()[0] == kEdge);

    CHECK(f.controller.applyOnce());
    f.controller.setApplyOnce(false);
    CHECK_FALSE(f.controller.applyOnce());
    CHECK(f.controller.pins().size() == 2);

    const auto preset = core::ops::ChangeSet::fromJson(f.state.changes().toJson());
    REQUIRE(preset);
    const auto plan = core::startPinsPlanFromOperations(preset->operations());
    REQUIRE(plan);
    CHECK(plan->pins.size() == 2);
    CHECK_FALSE(plan->applyOnce);

    f.controller.setMode(StartPinsController::Mode::Windows);
    CHECK(f.state.changes().empty());
    CHECK(f.controller.mode() == StartPinsController::Mode::Windows);
}

TEST_CASE("taskbar pins: Microsoft's layout that replaces the default pins, read back from the queue (D-083)") {
    core::TaskbarPinsPlan plan;
    plan.pins = {core::fileExplorerPin(),
                 StartApp{StartApp::Kind::Packaged, L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", L"Terminal"},
                 StartApp{StartApp::Kind::DesktopId, L"MSEdge", L"Edge"},
                 StartApp{StartApp::Kind::DesktopLink, L"%ALLUSERSPROFILE%\\Microsoft\\Windows\\Start Menu\\Programs\\A & B.lnk", L"A"}};
    const std::string xml = core::taskbarLayoutXml(plan);
    CHECK(xml.find("PinListPlacement=\"Replace\"") != std::string::npos);
    CHECK(xml.find("<taskbar:DesktopApp DesktopApplicationID=\"Microsoft.Windows.Explorer\" PinGeneration=\"1\"/>") != std::string::npos);
    CHECK(xml.find("<taskbar:UWA AppUserModelID=\"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App\" PinGeneration=\"1\"/>") != std::string::npos);
    CHECK(xml.find("A &amp; B.lnk") != std::string::npos);
    CHECK(xml.find("<!--") == std::string::npos); // Explorer drops a layout with comments

    const auto ops = core::taskbarPinsOperations(plan);
    REQUIRE(ops.size() == 3);
    CHECK(ops[0].target == L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer::LockedStartLayout");
    CHECK(ops[0].value == L"dword:00000001");
    const auto path = core::registryWriteFrom(ops[1].target, ops[1].value);
    REQUIRE(path);
    CHECK(path->name == L"StartLayoutFile");
    CHECK(ops[2].target == L"ProgramData\\WinLove\\TaskbarLayout.xml");
    CHECK(core::taskbarPinsSlots().size() == 3);

    const auto back = core::taskbarPlanFromOperations(ops);
    REQUIRE(back);
    REQUIRE(back->pins.size() == 4);
    CHECK(back->pins[0] == core::fileExplorerPin());
    CHECK(back->pins[0].name == L"File Explorer");
    CHECK(back->pins[1].kind == StartApp::Kind::Packaged);
    CHECK(back->pins[3].id == L"%ALLUSERSPROFILE%\\Microsoft\\Windows\\Start Menu\\Programs\\A & B.lnk");
    CHECK(back->userMayUnpin);

    // Nothing pinned: Microsoft's "#leaveempty"; pins without PinGeneration stay pinned.
    const auto empty = core::taskbarPlanFromXml(core::taskbarLayoutXml({}));
    REQUIRE(empty);
    CHECK(empty->pins.empty());
    CHECK(core::taskbarLayoutXml({}).find("#leaveempty") != std::string::npos);
    plan.userMayUnpin = false;
    const auto kept = core::taskbarPlanFromXml(core::taskbarLayoutXml(plan));
    REQUIRE(kept);
    CHECK_FALSE(kept->userMayUnpin);
    CHECK_FALSE(core::taskbarPlanFromOperations({}).has_value());
    CHECK_FALSE(core::taskbarPlanFromXml("<LayoutModificationTemplate/>").has_value());
}

TEST_CASE("taskbar pins controller: modes, File Explorer first, users may unpin; Start stays its own (D-083)") {
    Fixture f;
    StartPinsController taskbar(f.state, [](std::function<void()> fn) { fn(); }, StartPinsController::Surface::Taskbar, &f.controller);
    CHECK(taskbar.surface() == StartPinsController::Surface::Taskbar);
    CHECK(taskbar.mode() == StartPinsController::Mode::Windows);

    taskbar.setMode(StartPinsController::Mode::Custom); // starts from File Explorer
    CHECK(taskbar.mode() == StartPinsController::Mode::Custom);
    REQUIRE(taskbar.pins().size() == 1);
    CHECK(taskbar.pins().front().id == L"Microsoft.Windows.Explorer");
    CHECK(f.controller.mode() == StartPinsController::Mode::Windows); // the Start plan is another one
    CHECK(f.state.changes().find(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\Explorer::StartLayoutFile"));

    taskbar.add(StartApp{StartApp::Kind::Packaged, L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", L"Terminal"});
    taskbar.move(1, -1);
    REQUIRE(taskbar.pins().size() == 2);
    CHECK(taskbar.pins().front().kind == StartApp::Kind::Packaged);
    CHECK(taskbar.applyOnce()); // users may unpin: PinGeneration
    taskbar.setApplyOnce(false);
    const auto* file = f.state.changes().find(OpKind::WriteFile, L"ProgramData\\WinLove\\TaskbarLayout.xml");
    REQUIRE(file);
    CHECK(file->value.find(L"PinGeneration") == std::wstring::npos);
    CHECK_FALSE(taskbar.applyOnce());

    taskbar.setMode(StartPinsController::Mode::Empty);
    CHECK(taskbar.mode() == StartPinsController::Mode::Empty);
    CHECK(f.state.changes().find(OpKind::WriteFile, L"ProgramData\\WinLove\\TaskbarLayout.xml")->value.find(L"#leaveempty") !=
          std::wstring::npos);
    CHECK(taskbar.changedCount() == 1);

    f.controller.setMode(StartPinsController::Mode::Empty);
    taskbar.setMode(StartPinsController::Mode::Windows);
    CHECK(taskbar.changedCount() == 0);
    CHECK_FALSE(f.state.changes().find(OpKind::WriteFile, L"ProgramData\\WinLove\\TaskbarLayout.xml"));
    CHECK(f.controller.mode() == StartPinsController::Mode::Empty); // untouched
}
