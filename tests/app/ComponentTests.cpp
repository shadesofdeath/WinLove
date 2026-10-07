// P07: the AppX catalog (parse, longest-prefix lookup, fallback group) and the component
// controller (grouping, tri-state group check, queue operations).
#include "app/catalog/AppxCatalog.h"
#include "app/controllers/ComponentController.h"
#include "app/pages/ApplyPage.h"
#include "base/Utf8.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/image/dism/StoreShrink.h"

#include "base/Utf8.h"
#include "core/ops/Planner.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::ChangeSet;
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
    REQUIRE(catalog.groups().size() == 10);
    CHECK(catalog.components().size() == 49); // nothing skipped
    for (const auto& entry : catalog.components()) {
        CAPTURE(entry.id);
        CHECK_FALSE(entry.notes.tr.empty());
        CHECK_FALSE(entry.notes.en.empty());
        if (entry.kind == ComponentCatalogEntry::Kind::Remove) {
            CHECK(core::validateComponentRecipe(entry.recipe));
            // Something to find in the image: a path, or a package the component store knows (D-059);
            // or offered on every image (D-070: what the update scheduler installs).
            CHECK((entry.always || !entry.recipe.paths.empty() || !entry.recipe.packages.empty() ||
                   !entry.recipe.driverClasses.empty()));
            CHECK(entry.deep == !entry.recipe.driverClasses.empty());
            // What a working PC needs is never on offer (user, 2026-10-02): network and storage
            // drivers, phones / cameras (MTP), BitLocker.
            for (const auto& family : entry.recipe.packages) {
                CAPTURE(family);
                for (const wchar_t* needed : {L"-Wifi-", L"-Ethernet-", L"Portable-Devices", L"WPD-", L"SecureStartup", L"Storage"}) {
                    CHECK(family.find(needed) == std::wstring::npos);
                }
            }
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
    // Windows 10 keeps the setup file in SysWOW64: without that path the component was never offered there.
    CHECK(std::ranges::find(onedrive->recipe.paths, L"Windows\\SysWOW64\\OneDriveSetup.exe") != onedrive->recipe.paths.end());
    CHECK_FALSE(onedrive->always);

    // The new Outlook installs itself during OOBE: Windows 11 through a scheduler key, Windows 10
    // through a registration file that honours "deprovisioned" and Microsoft's documented block value.
    const auto* outlook = catalog.find("outlook-install");
    REQUIRE(outlook);
    CHECK(outlook->always);
    REQUIRE(outlook->recipe.registry.size() == 4);
    CHECK(outlook->recipe.registry[0].kind == core::RegistryWrite::Kind::DeleteKey);
    CHECK(outlook->recipe.registry[0].key.ends_with(L"\\UScheduler_Oobe\\OutlookUpdate"));
    const auto& block = outlook->recipe.registry[1];
    CHECK(block.kind == core::RegistryWrite::Kind::Set);
    CHECK(block.name == L"BlockedOobeUpdaters");
    CHECK(block.type == REG_SZ);
    REQUIRE(block.data.size() >= 2);
    CHECK(std::wstring(reinterpret_cast<const wchar_t*>(block.data.data()), block.data.size() / 2 - 1) ==
          LR"(["MS_Outlook"])");
    CHECK(outlook->recipe.registry[3].kind == core::RegistryWrite::Kind::CreateKey);
    CHECK(outlook->recipe.registry[3].key.ends_with(L"\\Deprovisioned\\Microsoft.OutlookForWindows_8wekyb3d8bbwe"));
    const auto* update = catalog.find("edge-update");
    REQUIRE(update);
    CHECK(update->recipe.registry.front().kind == core::RegistryWrite::Kind::DeleteKey);
    CHECK(catalog.find("component-store")->kind == ComponentCatalogEntry::Kind::Cleanup);
    REQUIRE(catalog.find("component-store-shrink"));
    CHECK(catalog.find("component-store-shrink")->kind == ComponentCatalogEntry::Kind::Shrink);
    CHECK(catalog.find("component-store-shrink")->risk == core::ops::Risk::High);
    CHECK_FALSE(catalog.find("defender")); // not a removable package on Windows 11 24H2+ (D-031)
    // D-063: Defender from the root — packages, files, service keys, its app; a preset carries it all.
    const auto* defender = catalog.find("defender-full");
    REQUIRE(defender);
    CHECK(defender->risk == core::ops::Risk::High);
    CHECK(defender->recipe.appx == std::vector<std::wstring>{L"Microsoft.SecHealthUI"});
    CHECK(std::ranges::count(defender->recipe.paths, std::wstring(L"Program Files\\Windows Defender")) == 1);
    CHECK(std::ranges::any_of(defender->recipe.registry, [](const core::RegistryWrite& w) {
        return w.kind == core::RegistryWrite::Kind::DeleteKey && w.key.ends_with(L"\\Services\\WinDefend");
    }));
    const auto json = core::componentRecipeToJson(defender->recipe);
    const auto back = core::componentRecipeFromJson(json);
    REQUIRE(back);
    CHECK(*back == defender->recipe);
    // What it deletes a later cumulative update needs: it runs after the updates of the same run.
    CHECK(defender->recipe.afterUpdates);
    CHECK(core::ops::phaseOf(core::ops::Operation{core::ops::OpKind::RemoveComponent, L"defender-full", utf8::toWide(json)}) ==
          core::ops::Phase::DeepRemove);
    core::ComponentRecipe bad = defender->recipe;
    bad.appx = {L"..\\evil"};
    CHECK_FALSE(core::validateComponentRecipe(bad));

    // Bad entries are dropped one by one, the rest of the file still loads.
    const auto partial = ComponentCatalog::parse(R"({"format":"winlove.catalog.components",
        "groups":[{"id":"g","tr":"G","en":"G"}],
        "components":[
          {"id":"ok","group":"g","tr":"Tamam","en":"Fine","paths":["Program Files\\X"]},
          {"id":"root","group":"g","tr":"Kök","en":"Root","paths":["Windows"]},
          {"id":"package","group":"g","tr":"Paket","en":"Package","packages":["Some-Package"]},
          {"id":"nothing","group":"g","tr":"Hiç","en":"Nothing","registry":[{"key":"HKLM\\SOFTWARE\\X","delete":true}]},
          {"id":"ok","group":"g","tr":"Yine","en":"Again","paths":["Program Files\\Y"]},
          {"id":"lost","group":"nowhere","tr":"A","en":"B","paths":["Program Files\\Z"]},
          {"id":"sam","group":"g","tr":"A","en":"B","paths":["Program Files\\Z"],"registry":[{"key":"HKLM\\SAM\\x","delete":true}]}]})");
    REQUIRE(partial);
    REQUIRE(partial->components().size() == 2); // a package alone is enough (D-059); registry alone is not
    CHECK(partial->components().front().id == "ok");
    CHECK(partial->components().back().id == "package");
    CHECK_FALSE(ComponentCatalog::parse(R"({"format":"winlove.catalog.appx"})"));
}

TEST_CASE("ComponentController: system components the image has, the cleanup, and their queue operations") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); },
                                   shippedComponents());
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    // Known before the app list arrives: the controller has nothing to read itself.
    state.setSystemComponents(AppState::SystemComponents{
        L"C:\\m", {{"edge", {true, 800}}, {"onedrive", {true, 90}}, {"winre", {false, 0}}, {"component-store-shrink", {true, 3000}}}});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\m", {makeApp(L"Microsoft.GamingApp", 400)}, {}});

    const auto groups = controller.groups();
    REQUIRE(groups.size() == 5); // Sistem Bileşenleri, Windows'un Kendiliğinden Kurdukları, Gizlilik, Temizlik, then the apps (xbox)
    const auto& system = groups[0];
    CHECK(system.name == L"Sistem Bileşenleri");
    REQUIRE(system.items.size() == 1); // what is present
    CHECK(system.items[0].name == L"Microsoft Edge");
    CHECK(system.items[0].kind == ComponentController::Item::Kind::System);
    CHECK(system.items[0].size == 800);
    CHECK(system.size == 800);
    // D-070: OneDrive (present) and every app the update scheduler installs (always on offer), in catalog order.
    const auto& self = groups[1];
    CHECK(self.name == L"Windows'un Kendiliğinden Kurdukları");
    REQUIRE(self.items.size() == 7);
    CHECK(self.size == 90);
    CHECK(self.items[0].system->id == "onedrive");
    CHECK(self.items[0].contents.size() == 5); // two packages, the setup file of either Windows, the shortcut
    CHECK_FALSE(self.items[0].notes.empty());
    // Nothing of it on disk in this image, offered all the same; what it changes is listed.
    CHECK(self.items[1].system->id == "outlook-install");
    CHECK(self.items[1].size == 0);
    CHECK(self.items[1].contents.size() == 7); // the registration folder, the spare PWA, the placeholder app, four registry changes
    CHECK(self.items[2].system->id == "teams-install");
    CHECK(self.items[5].system->id == "m365-install");
    // D-075: telemetry is always on offer and switches services off; it never removes a package.
    const auto& privacy = groups[2];
    REQUIRE(privacy.items.size() == 1);
    CHECK(privacy.items[0].system->id == "telemetry");
    CHECK(privacy.items[0].system->recipe.packages.empty());
    CHECK(privacy.items[0].system->recipe.registry.size() == 3);
    REQUIRE(groups[3].items.size() == 2);
    const auto& cleanup = groups[3].items.front();
    CHECK(cleanup.kind == ComponentController::Item::Kind::Cleanup);
    CHECK(cleanup.size == 0);
    // D-079: what shrinking frees, as the probe measured it.
    const auto& shrink = groups[3].items.back();
    CHECK(shrink.kind == ComponentController::Item::Kind::Shrink);
    CHECK(shrink.size == 3000);
    CHECK(shrink.risk == core::ops::Risk::High);
    // Group indexes never collide with the AppX catalog's.
    CHECK(groups[4].name == L"Xbox ve Oyun");
    CHECK(system.catalogIndex != groups[4].catalogIndex);
    CHECK(self.catalogIndex != system.catalogIndex);
    CHECK(self.catalogIndex != groups[4].catalogIndex);

    controller.toggle(self.items[0]);
    const auto* op = state.changes().find(OpKind::RemoveComponent, L"onedrive");
    REQUIRE(op);
    CHECK(op->risk == core::ops::Risk::Low);
    CHECK(op->sizeDelta == -90);
    const auto recipe = core::componentRecipeFromJson(utf8::fromWide(op->value));
    REQUIRE(recipe);
    CHECK(recipe->title == L"OneDrive kurulumu");
    CHECK(recipe->packages.size() == 2);
    CHECK(recipe->paths.size() == 3);
    CHECK(recipe->paths.front() == L"Windows\\System32\\OneDriveSetup.exe");
    CHECK(ApplyPage::displayName(state, *op) == L"OneDrive kurulumu");
    CHECK(controller.queued(self.items[0]));
    CHECK_FALSE(controller.queued(self.items[1]));
    CHECK(controller.check(self) == ComponentController::Check::Partial);

    controller.toggle(cleanup);
    const auto* clean = state.changes().find(OpKind::CleanupImage, L"component-store");
    REQUIRE(clean);
    CHECK(core::storeCleanupFromJson(utf8::fromWide(clean->value))->resetBase);
    CHECK(ApplyPage::displayName(state, *clean) == cleanup.name);
    CHECK(controller.queuedCount() == 2);
    CHECK(controller.queuedBytes() == 90);

    // The shrink brings automatic updates off with it, in one queue edit; out again, the update
    // setting stays (Ayarlar shows it).
    const auto version = state.changes().version();
    controller.toggle(shrink);
    CHECK(state.changes().version() == version + 1);
    const auto* shrunk = state.changes().find(OpKind::ShrinkStore, L"component-store-shrink");
    REQUIRE(shrunk);
    CHECK(shrunk->risk == core::ops::Risk::High);
    CHECK(core::storeShrinkFromJson(utf8::fromWide(shrunk->value))->title == shrink.name);
    CHECK(ApplyPage::displayName(state, *shrunk) == shrink.name);
    const auto* updates =
        state.changes().find(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU::NoAutoUpdate");
    REQUIRE(updates);
    CHECK(updates->value == L"dword:00000001");
    const std::wstring updatesTarget = updates->target; // the queue's storage moves when it changes
    CHECK(controller.queuedCount() == 3);
    CHECK(controller.queuedBytes() == 3090);
    controller.toggle(shrink);
    CHECK_FALSE(state.changes().find(OpKind::ShrinkStore, L"component-store-shrink"));
    CHECK(state.changes().find(OpKind::SetRegistryValue, updatesTarget));
    controller.toggle(shrink); // the setting is there already: not queued twice
    CHECK(state.changes().count(OpKind::SetRegistryValue) == 1);
    controller.toggle(shrink);

    controller.toggle(self.items[0]); // again: out of the queue
    CHECK_FALSE(state.changes().find(OpKind::RemoveComponent, L"onedrive"));
    controller.toggleGroup(self);
    CHECK(controller.check(self) == ComponentController::Check::On);
    controller.resetChanges();
    REQUIRE(state.changes().size() == 1); // the update setting belongs to Ayarlar
    CHECK(state.changes().operations().front().kind == OpKind::SetRegistryValue);

    // Another image: what was known about the old one is gone with it.
    state.setMounted(MountedImage{L"C:\\m2", L"C:\\w\\install.wim", 2, L"Home"});
    CHECK_FALSE(state.systemComponents());
    CHECK(controller.groups().empty());
}

TEST_CASE("ComponentController: the apps DISM refuses are picked like any other (D-038)") {
    AppState state{scratch(L"recent-refused.json"), scratch(L"settings-refused.json")};
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); });
    core::SourceInfo source;
    source.path = scratch(L"refused");
    core::ImageInfo image;
    image.index = 1;
    image.build = 26200; // DISM answers 0x80073CFA here; the Applier then removes them itself
    source.install.images.push_back(image);
    state.setSource(source);
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\m",
                                         {makeApp(L"Microsoft.WindowsStore", 50), makeApp(L"Microsoft.SecHealthUI", 9),
                                          makeApp(L"Microsoft.DesktopAppInstaller", 20)},
                                         {}});
    const auto groups = controller.groups();
    REQUIRE(groups.size() == 1);
    const auto& system = groups.front();
    REQUIRE(system.items.size() == 3);
    const auto& security = *std::ranges::find(system.items, std::wstring(L"Microsoft.SecHealthUI"),
                                              &ComponentController::Item::identity);
    controller.toggle(security);
    REQUIRE(controller.queued(security));
    CHECK(state.changes().find(OpKind::RemoveAppx, security.packageName)->risk == core::ops::Risk::High);
    CHECK_FALSE(security.notes.empty()); // what removing it means is said before it is picked
    CHECK(controller.check(system) == ComponentController::Check::Partial);
    controller.toggleGroup(system);
    CHECK(state.changes().size() == 3);
    CHECK(controller.check(system) == ComponentController::Check::On);
}

TEST_CASE("ComponentController: a preset's saved recipes are replaced by the catalog's current ones (D-076)") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); },
                                   shippedComponents());
    // As the user's preset of 2026-10-06 carried them: Outlook without the InboxApps spare
    // package, telemetry still removing the TroubleShooting package.
    ChangeSet saved;
    saved.add({OpKind::RemoveComponent, L"outlook-install",
               LR"({"appx":["Microsoft.OutlookForWindows"],"paths":["ProgramData\\USOPrivate\\ExpeditedAppRegistrations\\MS_Outlook"],"title":"Outlook"})"});
    saved.add({OpKind::RemoveComponent, L"telemetry",
               LR"({"packages":["Microsoft-OneCore-TroubleShooting-Package"],"title":"Telemetri"})"});
    saved.add({OpKind::RemoveComponent, L"gone-from-catalog", LR"({"paths":["Program Files\\Old"],"title":"Old"})"});
    saved.add({OpKind::RemoveAppx, L"Microsoft.GamingApp_1_neutral__8wekyb3d8bbwe", L"Xbox"});

    const ChangeSet current = controller.withCurrentRecipes(saved);
    REQUIRE(current.size() == 4);
    const auto outlook = core::componentRecipeFromJson(utf8::fromWide(current.find(OpKind::RemoveComponent, L"outlook-install")->value));
    REQUIRE(outlook);
    CHECK(std::ranges::find(outlook->paths, L"Windows\\InboxApps\\OutlookPWA.msix") != outlook->paths.end());
    CHECK(outlook->title == L"Yeni Outlook'un kendiliğinden kurulması");
    const auto telemetry = core::componentRecipeFromJson(utf8::fromWide(current.find(OpKind::RemoveComponent, L"telemetry")->value));
    REQUIRE(telemetry);
    CHECK(telemetry->packages.empty());
    CHECK(current.find(OpKind::RemoveComponent, L"gone-from-catalog")->value == saved.operations()[2].value);
    CHECK(current.find(OpKind::RemoveAppx, L"Microsoft.GamingApp_1_neutral__8wekyb3d8bbwe"));
}
