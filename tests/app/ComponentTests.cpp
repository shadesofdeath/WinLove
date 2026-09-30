// P07: the AppX catalog (parse, longest-prefix lookup, fallback group) and the component
// controller (grouping, tri-state group check, queue operations).
#include "app/catalog/AppxCatalog.h"
#include "app/controllers/ComponentController.h"
#include "app/pages/ApplyPage.h"
#include "base/Utf8.h"
#include "core/image/dism/StoreCleanup.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;

namespace {
std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"components";
    std::filesystem::create_directories(dir);
    return dir / name;
}

AppxCatalog shippedCatalog() {
    auto catalog = AppxCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/appx.json"));
    REQUIRE(catalog);
    return std::move(*catalog);
}

core::AppxComponent makeApp(const wchar_t* identity, std::uint64_t size) {
    core::AppxComponent c;
    c.package.displayName = identity;
    c.package.packageName = std::wstring(identity) + L"_1.0.0.0_neutral_~_8wekyb3d8bbwe";
    c.size = size;
    return c;
}
} // namespace

TEST_CASE("AppX catalog: shipped file parses; longest prefix wins; unknown apps fall back") {
    const auto catalog = shippedCatalog();
    CHECK(catalog.groups().back().id == "other");
    const auto* vclibs = catalog.find(L"Microsoft.VCLibs.140.00.UWPDesktop");
    REQUIRE(vclibs);
    CHECK(vclibs->group == "frameworks");
    CHECK(vclibs->risk == core::ops::Risk::High);
    const auto* overlay = catalog.find(L"Microsoft.XboxGamingOverlay");
    REQUIRE(overlay);
    CHECK(overlay->match == L"Microsoft.XboxGamingOverlay"); // not the shorter "Microsoft.XboxGameOverlay"
    CHECK(catalog.find(L"microsoft.windowsstore")); // identities compare case-insensitively
    CHECK_FALSE(catalog.find(L"Contoso.Unknown"));
    CHECK_FALSE(AppxCatalog::parse(R"({"format":"something.else"})"));
}

TEST_CASE("ComponentController groups apps, checks groups tri-state and queues RemoveAppx") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); });
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\m",
                                         {makeApp(L"Microsoft.GamingApp", 400), makeApp(L"Microsoft.XboxGamingOverlay", 90),
                                          makeApp(L"Microsoft.WindowsStore", 50), makeApp(L"Contoso.Unknown", 3)},
                                         {}});
    const auto groups = controller.groups();
    REQUIRE(groups.size() == 3); // xbox, system, other — empty groups are dropped
    CHECK(groups.front().name == L"Xbox ve Oyun");
    CHECK(groups.front().size == 490);
    CHECK(groups.back().items.front().name == L"Contoso.Unknown");

    const auto& xbox = groups.front();
    CHECK(controller.check(xbox) == ComponentController::Check::Off);
    controller.toggle(xbox.items.front());
    CHECK(controller.check(xbox) == ComponentController::Check::Partial);
    controller.toggleGroup(xbox); // partial → all
    CHECK(controller.check(xbox) == ComponentController::Check::On);
    CHECK(controller.queuedCount() == 2);
    CHECK(controller.queuedBytes() == 490);
    const auto* op = state.changes().find(OpKind::RemoveAppx, xbox.items.front().packageName);
    REQUIRE(op);
    CHECK(op->sizeDelta < 0);
    // The Apply lists show the catalog name, not the package full name.
    CHECK(ApplyPage::displayName(state, *op) == xbox.items.front().name);
    CHECK(ApplyPage::displayName(state, {OpKind::RemoveAppx, L"Contoso.App_1.0.0.0_neutral_~_abc"}) == L"Contoso.App");
    controller.toggleGroup(xbox); // all → none
    CHECK(controller.queuedCount() == 0);

    const auto& system = groups[1];
    controller.toggle(system.items.front());
    CHECK(state.changes().find(OpKind::RemoveAppx, system.items.front().packageName)->risk == core::ops::Risk::High);
    controller.resetChanges();
    CHECK(state.changes().empty());
}

namespace {
ComponentCatalog shippedComponents() {
    auto catalog = ComponentCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/components.json"));
    REQUIRE(catalog);
    return std::move(*catalog);
}
} // namespace

TEST_CASE("components catalog: the shipped file parses, every recipe is one the engine accepts") {
    const auto catalog = shippedComponents();
    REQUIRE(catalog.groups().size() == 2);
    CHECK(catalog.components().size() == 6); // nothing skipped
    for (const auto& entry : catalog.components()) {
        CAPTURE(entry.id);
        CHECK_FALSE(entry.notes.tr.empty());
        CHECK_FALSE(entry.notes.en.empty());
        if (entry.kind == ComponentCatalogEntry::Kind::Remove) {
            CHECK(core::validateComponentRecipe(entry.recipe));
            CHECK_FALSE(entry.recipe.paths.empty());
            // The recipe survives the trip through a queue operation / preset file.
            auto recipe = entry.recipe;
            recipe.title = entry.name.tr;
            CHECK(*core::componentRecipeFromJson(core::componentRecipeToJson(recipe)) == recipe);
        }
    }
    const auto* onedrive = catalog.find("onedrive");
    REQUIRE(onedrive);
    CHECK(onedrive->recipe.packages.size() == 2);
    CHECK(onedrive->recipe.registry.front().kind == core::RegistryWrite::Kind::DeleteValue);
    CHECK(onedrive->recipe.registry.front().key == L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    const auto* update = catalog.find("edge-update");
    REQUIRE(update);
    CHECK(update->recipe.registry.front().kind == core::RegistryWrite::Kind::DeleteKey);
    CHECK(catalog.find("component-store")->kind == ComponentCatalogEntry::Kind::Cleanup);
    CHECK_FALSE(catalog.find("defender")); // not a removable package on Windows 11 24H2+ (D-031)

    // Bad entries are dropped one by one, the rest of the file still loads.
    const auto partial = ComponentCatalog::parse(R"({"format":"winlove.catalog.components",
        "groups":[{"id":"g","tr":"G","en":"G"}],
        "components":[
          {"id":"ok","group":"g","tr":"Tamam","en":"Fine","paths":["Program Files\\X"]},
          {"id":"root","group":"g","tr":"Kök","en":"Root","paths":["Windows"]},
          {"id":"nopath","group":"g","tr":"Yolsuz","en":"No path","packages":["Some-Package"]},
          {"id":"ok","group":"g","tr":"Yine","en":"Again","paths":["Program Files\\Y"]},
          {"id":"lost","group":"nowhere","tr":"A","en":"B","paths":["Program Files\\Z"]},
          {"id":"sam","group":"g","tr":"A","en":"B","paths":["Program Files\\Z"],"registry":[{"key":"HKLM\\SAM\\x","delete":true}]}]})");
    REQUIRE(partial);
    REQUIRE(partial->components().size() == 1);
    CHECK(partial->components().front().id == "ok");
    CHECK_FALSE(ComponentCatalog::parse(R"({"format":"winlove.catalog.appx"})"));
}

TEST_CASE("ComponentController: system components the image has, the cleanup, and their queue operations") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); },
                                   shippedComponents());
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    // Known before the app list arrives: the controller has nothing to read itself.
    state.setSystemComponents(AppState::SystemComponents{
        L"C:\\m", {{"edge", {true, 800}}, {"onedrive", {true, 90}}, {"winre", {false, 0}}}});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\m", {makeApp(L"Microsoft.GamingApp", 400)}, {}});

    const auto groups = controller.groups();
    REQUIRE(groups.size() == 3); // Sistem Bileşenleri, Temizlik, then the apps (xbox)
    const auto& system = groups[0];
    CHECK(system.name == L"Sistem Bileşenleri");
    REQUIRE(system.items.size() == 2); // what is present, in catalog order
    CHECK(system.items[0].name == L"Microsoft Edge");
    CHECK(system.items[0].kind == ComponentController::Item::Kind::System);
    CHECK(system.items[0].size == 800);
    CHECK(system.size == 890);
    CHECK(system.items[1].contents.size() == 3); // two packages and the setup file
    CHECK_FALSE(system.items[1].notes.empty());
    const auto& cleanup = groups[1].items.front();
    CHECK(cleanup.kind == ComponentController::Item::Kind::Cleanup);
    CHECK(cleanup.size == 0);
    // Group indexes never collide with the AppX catalog's.
    CHECK(groups[2].name == L"Xbox ve Oyun");
    CHECK(system.catalogIndex != groups[2].catalogIndex);
    CHECK(groups[1].catalogIndex != system.catalogIndex);

    controller.toggle(system.items[1]);
    const auto* op = state.changes().find(OpKind::RemoveComponent, L"onedrive");
    REQUIRE(op);
    CHECK(op->risk == core::ops::Risk::Low);
    CHECK(op->sizeDelta == -90);
    const auto recipe = core::componentRecipeFromJson(utf8::fromWide(op->value));
    REQUIRE(recipe);
    CHECK(recipe->title == L"OneDrive kurulumu");
    CHECK(recipe->packages.size() == 2);
    CHECK(recipe->paths == std::vector<std::wstring>{L"Windows\\System32\\OneDriveSetup.exe"});
    CHECK(ApplyPage::displayName(state, *op) == L"OneDrive kurulumu");
    CHECK(controller.queued(system.items[1]));
    CHECK_FALSE(controller.queued(system.items[0]));
    CHECK(controller.check(system) == ComponentController::Check::Partial);

    controller.toggle(cleanup);
    const auto* clean = state.changes().find(OpKind::CleanupImage, L"component-store");
    REQUIRE(clean);
    CHECK(core::storeCleanupFromJson(utf8::fromWide(clean->value))->resetBase);
    CHECK(ApplyPage::displayName(state, *clean) == cleanup.name);
    CHECK(controller.queuedCount() == 2);
    CHECK(controller.queuedBytes() == 90);

    controller.toggle(system.items[1]); // again: out of the queue
    CHECK_FALSE(state.changes().find(OpKind::RemoveComponent, L"onedrive"));
    controller.toggleGroup(system);
    CHECK(controller.check(system) == ComponentController::Check::On);
    controller.resetChanges();
    CHECK(state.changes().empty());

    // Another image: what was known about the old one is gone with it.
    state.setMounted(MountedImage{L"C:\\m2", L"C:\\w\\install.wim", 2, L"Home"});
    CHECK_FALSE(state.systemComponents());
    CHECK(controller.groups().empty());
}
