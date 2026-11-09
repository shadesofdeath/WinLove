// P07: the AppX catalog (parse, longest-prefix lookup, fallback group) and the component
// controller (grouping, tri-state group check, queue operations).
#include "app/catalog/AppxCatalog.h"
#include "app/controllers/ComponentController.h"
#include "app/pages/ApplyPage.h"
#include "base/Utf8.h"
#include "core/image/ComponentStore.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/image/dism/StoreShrink.h"

#include "base/Utf8.h"
#include "core/ops/Planner.h"

#include <doctest.h>

#include <cwctype>
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

TEST_CASE("ComponentController groups apps by the reference tool's categories, checks groups tri-state and queues RemoveAppx") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    auto system = ComponentCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/components.json"));
    REQUIRE(system);
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); },
                                   std::move(*system));
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\m",
                                         {makeApp(L"Microsoft.GamingApp", 400), makeApp(L"Microsoft.XboxGamingOverlay", 90),
                                          makeApp(L"Microsoft.WindowsStore", 50), makeApp(L"Contoso.Unknown", 3)},
                                         {}});
    const auto groups = controller.groups();
    // D-110: Eğlence ve Kişiselleştirme (the Xbox apps), Hizmetler ve Platform (the Store), Kategorisiz
    // — the reference tool's order, empty categories dropped; the items by name.
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].name == L"Eğlence ve Kişiselleştirme");
    CHECK(groups[0].size == 490);
    CHECK(groups[0].items.front().name == L"Xbox");
    CHECK(groups[0].items.front().tier == Tier::General);
    CHECK(groups[1].name == L"Hizmetler ve Platform");
    CHECK(groups[1].items.front().tier == Tier::NotRecommended);
    CHECK(groups[2].name == L"Kategorisiz");
    CHECK(groups[2].items.front().name == L"Contoso.Unknown");
    CHECK(groups[2].items.front().tier == Tier::None);

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

    const auto& services = groups[1];
    controller.toggle(services.items.front());
    CHECK(state.changes().find(OpKind::RemoveAppx, services.items.front().packageName)->risk == core::ops::Risk::High);
    controller.resetChanges();
    CHECK(state.changes().empty());

    // D-110: a template adds what it takes (Genel takes Gizlilik's too), never the red rows.
    CHECK(controller.applyTemplate(Tier::Privacy) == 0); // nothing green in this list
    CHECK(controller.applyTemplate(Tier::General) == 2); // the two Xbox apps
    CHECK(controller.applyTemplate(Tier::Light) == 0);
    CHECK_FALSE(controller.queued(services.items.front()));
    CHECK(templateTakes(Tier::Light, Tier::Privacy));
    CHECK_FALSE(templateTakes(Tier::Privacy, Tier::General));
    CHECK_FALSE(templateTakes(Tier::Light, Tier::NotRecommended));
}

namespace {
ComponentCatalog shippedComponents() {
    auto catalog = ComponentCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/components.json"));
    REQUIRE(catalog);
    return std::move(*catalog);
}
} // namespace

TEST_CASE("components catalog: ASP.NET leaves the System.Web assemblies Windows PowerShell needs (D-117)") {
    // VM test 2026-11-08: "system.web*" took System.Web; ConvertFrom-Json (System.Web.Extensions → System.Web)
    // failed and WinLove's own setup screens came up empty. They go only with the whole .NET Framework.
    const auto catalog = shippedComponents();
    const auto* aspnet = catalog.find("dotnet-aspnet");
    REQUIRE(aspnet != nullptr);
    const auto takes = [&](std::wstring_view identity) {
        return std::ranges::any_of(aspnet->store, [&](const std::wstring& p) { return core::ComponentStoreIndex::storePatternMatches(p, identity); });
    };
    for (const wchar_t* name : {L"system.web", L"system.web.services", L"system.web.regularexpressions", L"system.web.applicationservices",
                                L"system.web.extensions"}) {
        for (const wchar_t* token : {L"b03f5f7f11d50a3a", L"b77a5c561934e089", L"31bf3856ad364e35"}) {
            CAPTURE(std::wstring(name));
            CHECK_FALSE(takes(std::wstring(name) + L"|" + token + L"|amd64|neutral"));
        }
    }
    CHECK(takes(L"system.web.mobile|b03f5f7f11d50a3a|amd64|neutral"));
    CHECK(takes(L"aspnet_compiler|b03f5f7f11d50a3a|amd64|neutral"));
}

TEST_CASE("components catalog: EFS leaves the core, the LSA extension and the service app registration needs (D-118)") {
    // VM test 2026-11-08: "microsoft-windows-efs*" took efscore.dll / efssvc.dll / efslsaext.dll and the EFS
    // service. The app deployment service asks efswrt.dll (CdplIsSupported → feclient → EFS) each time it
    // registers a Store package; it failed with E_NOTIMPL, no package registered for a new account and
    // Setup looped on "Why did my PC restart?". Only the user interface goes now.
    const auto catalog = shippedComponents();
    const auto* efs = catalog.find("efs");
    REQUIRE(efs != nullptr);
    const auto takes = [&](std::wstring_view identity) {
        return std::ranges::any_of(efs->store, [&](const std::wstring& p) { return core::ComponentStoreIndex::storePatternMatches(p, identity); });
    };
    for (const wchar_t* name : {L"microsoft-windows-efs-core-library", L"microsoft-windows-efs-core-library.instrumentation",
                                L"microsoft-windows-efs-lsa-extension", L"microsoft-windows-efs-platform-extension",
                                L"microsoft-windows-efs-service", L"microsoft-windows-efs-util-library"}) {
        CAPTURE(std::wstring(name));
        CHECK_FALSE(takes(std::wstring(name) + L"|31bf3856ad364e35|amd64|neutral"));
    }
    CHECK(efs->recipe.services.empty());
    CHECK(takes(L"microsoft-windows-efs-ui|31bf3856ad364e35|amd64|neutral"));
    CHECK(takes(L"microsoft-windows-efs-rekeywiz|31bf3856ad364e35|amd64|neutral"));
}

TEST_CASE("components catalog: Edge Update leaves WebView2's registration under the EdgeUpdate key (D-118)") {
    // VM test 2026-11-08: the recipe deleted HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate whole; the WebView2
    // runtime stayed in Program Files (x86) but Clients\{F3017226-…} (how it is found) was gone with it.
    const auto catalog = shippedComponents();
    const auto* update = catalog.find("edge-update");
    REQUIRE(update != nullptr);
    const auto lowerOf = [](std::wstring text) {
        for (auto& c : text) {
            c = static_cast<wchar_t>(towlower(c));
        }
        return text;
    };
    bool ownClient = false;
    for (const auto& write : update->recipe.registry) {
        const std::wstring key = lowerOf(write.key);
        CAPTURE(key);
        CHECK_FALSE((write.kind == core::RegistryWrite::Kind::DeleteKey && key.ends_with(L"\\microsoft\\edgeupdate")));
        CHECK(key.find(L"{f3017226-fe2a-4295-8bdf-00c3a9a7e4c5}") == std::wstring::npos); // WebView2
        CHECK(key.find(L"{56eb18f8-b008-4cbd-b6d2-8c97fe7e9062}") == std::wstring::npos); // Edge: its own recipe
        ownClient = ownClient || key.find(L"{f3c4fe00-efd5-403b-9569-398a20f1ba4a}") != std::wstring::npos;
    }
    CHECK(ownClient);
}

TEST_CASE("components catalog: the shipped file parses, every recipe is one the engine accepts") {
    const auto catalog = shippedComponents();
    REQUIRE(catalog.groups().size() == 10);
    // Nothing skipped: every id of the file is in the catalog.
    {
        const std::string text = readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/components.json");
        std::size_t ids = 0;
        for (std::size_t at = text.find("{\"id\": \""); at != std::string::npos; at = text.find("{\"id\": \"", at + 1)) {
            const std::size_t start = at + 8;
            const std::string id = text.substr(start, text.find('"', start) - start);
            if (text.compare(text.rfind('\n', at) + 1, 4, "    ") == 0 && text.find("\"group\"", at) < text.find('\n', at)) {
                CAPTURE(id);
                CHECK(catalog.find(id) != nullptr);
                ++ids;
            }
        }
        CHECK(catalog.components().size() == ids);
        CHECK(ids > 150);
    }
    for (const auto& entry : catalog.components()) {
        CAPTURE(entry.id);
        if (entry.container) {
            CHECK(entry.store.empty());
            CHECK(entry.recipe.services.empty());
            continue; // D-113: a node over what is under it
        }
        CHECK_FALSE(entry.notes.tr.empty());
        CHECK_FALSE(entry.notes.en.empty());
        if (entry.kind == ComponentCatalogEntry::Kind::Remove && (!entry.store.empty() || !entry.recipe.services.empty())) {
            // D-113: the store names come from the image; what the entry itself carries must pass.
            auto rest = entry.recipe;
            rest.storeNames = {L"microsoft-windows-example"};
            CHECK(core::validateComponentRecipe(rest));
            for (const auto& pattern : entry.store) {
                CAPTURE(pattern);
                // What Windows cannot start without is only ever a locked row (shown, never removed).
                CHECK((entry.locked || !core::isProtectedStoreName(pattern.substr(0, pattern.find(L'*')))));
            }
            if (!entry.parent.empty()) {
                CHECK(catalog.find(entry.parent) != nullptr);
            }
            continue;
        }
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

    // D-110: the reference tool's categories in their order — API (OneDrive), Ağ (Edge), Donanım (the cross
    // device installer), Eğlence (Xbox), Tanılama (telemetry), Üretkenlik (the scheduler's apps).
    const auto groups = controller.groups();
    REQUIRE(groups.size() == 6);
    const auto& api = groups[0];
    CHECK(api.name == L"API ve Çalışma Zamanı");
    REQUIRE(api.items.size() == 1);
    CHECK(api.items[0].system->id == "onedrive");
    CHECK(api.items[0].tier == Tier::Privacy);
    CHECK(api.items[0].contents.size() == 5); // two packages, the setup file of either Windows, the shortcut
    CHECK_FALSE(api.items[0].notes.empty());
    CHECK(api.size == 90);
    const auto& network = groups[1];
    CHECK(network.name == L"Ağ");
    REQUIRE(network.items.size() == 1); // what is present
    CHECK(network.items[0].name == L"Microsoft Edge");
    CHECK(network.items[0].kind == ComponentController::Item::Kind::System);
    CHECK(network.items[0].size == 800);
    CHECK(groups[2].name == L"Donanım desteği");
    CHECK(groups[3].name == L"Eğlence ve Kişiselleştirme");
    // D-075: telemetry is always on offer and switches services off; it never removes a package.
    const auto& diagnostics = groups[4];
    CHECK(diagnostics.name == L"Tanılama ve Bakım");
    REQUIRE(diagnostics.items.size() == 1);
    CHECK(diagnostics.items[0].system->id == "telemetry");
    CHECK(diagnostics.items[0].system->recipe.packages.empty());
    CHECK(diagnostics.items[0].system->recipe.registry.size() == 3);
    // D-070: the apps the update scheduler installs are on offer whatever the image has, by name.
    const auto& self = groups[5];
    CHECK(self.name == L"Üretkenlik");
    REQUIRE(self.items.size() == 5);
    const auto outlook = std::ranges::find_if(self.items, [](const auto& i) { return i.system->id == "outlook-install"; });
    REQUIRE(outlook != self.items.end());
    CHECK(outlook->size == 0);
    CHECK(outlook->contents.size() == 7); // the registration folder, the spare PWA, the placeholder app, four registry changes

    // The store cleanup and the shrink are the removal level's (D-110), not rows of the tree.
    const auto mode = controller.modeItems();
    REQUIRE(mode.size() == 2);
    const auto& cleanup = mode.front();
    CHECK(cleanup.kind == ComponentController::Item::Kind::Cleanup);
    CHECK(cleanup.size == 0);
    // D-079: what shrinking frees, as the probe measured it.
    const auto& shrink = mode.back();
    CHECK(shrink.kind == ComponentController::Item::Kind::Shrink);
    CHECK(shrink.size == 3000);
    CHECK(shrink.risk == core::ops::Risk::High);
    CHECK(controller.level() == ComponentController::Level::DefaultPlus);
    CHECK(api.catalogIndex != network.catalogIndex);

    controller.toggle(api.items[0]);
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
    CHECK(controller.queued(api.items[0]));

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

    controller.toggle(api.items[0]); // again: out of the queue
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

TEST_CASE("ComponentController: a preset's app removals get this image's version of the app (audit A4)") {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); },
                                   shippedComponents());
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:/m",
                                         {makeApp(L"Microsoft.GamingApp", 400), makeApp(L"Microsoft.WindowsStore", 50)},
                                         {}});
    ChangeSet saved; // made on another build: other versions
    saved.add({OpKind::RemoveAppx, L"Microsoft.GamingApp_2410.1001.4.0_neutral_~_8wekyb3d8bbwe", L"Xbox"});
    saved.add({OpKind::RemoveAppx, L"Microsoft.WindowsStore_1.0.0.0_neutral_~_8wekyb3d8bbwe", L"Store"});
    saved.add({OpKind::RemoveAppx, L"Microsoft.BingNews_4.1.0.0_neutral_~_8wekyb3d8bbwe", L"News"});

    const ChangeSet current = controller.withCurrentRecipes(saved);
    CHECK(current.size() == 3);
    CHECK(current.find(OpKind::RemoveAppx, L"Microsoft.GamingApp_1.0.0.0_neutral_~_8wekyb3d8bbwe"));
    CHECK_FALSE(current.find(OpKind::RemoveAppx, L"Microsoft.GamingApp_2410.1001.4.0_neutral_~_8wekyb3d8bbwe"));
    CHECK(current.find(OpKind::RemoveAppx, L"Microsoft.WindowsStore_1.0.0.0_neutral_~_8wekyb3d8bbwe")); // same name
    CHECK(current.find(OpKind::RemoveAppx, L"Microsoft.BingNews_4.1.0.0_neutral_~_8wekyb3d8bbwe")); // not here: as saved
}

TEST_CASE("ComponentController: the reference tool's dependency tree from the store split (D-113)") {
    auto catalog = ComponentCatalog::parse(R"({"format":"winlove.catalog.components","version":1,
        "categories":[{"id":"api","tr":"API ve Çalışma Zamanı","en":"API"},{"id":"network","tr":"Ağ","en":"Network"}],
        "nodes":[{"id":"lockRequired","tr":"Gerekli","en":"Required"},{"id":"bits32","tr":" - 32 bit","en":" - 32-bit"}],
        "groups":[{"id":"g","tr":"G","en":"G"}],
        "components":[
          {"id":"ts","group":"g","category":"api","tier":"notRecommended","tr":"Görev Zamanlayıcısı","en":"Task Scheduler",
           "store":["microsoft-windows-taskscheduler-*"],"services":["Schedule"],"notes_tr":"n","notes_en":"n"},
          {"id":"ts-compat","group":"g","category":"api","tier":"light","parent":"ts","tr":"Eski arabirimler","en":"Legacy",
           "store":["microsoft-windows-taskscheduler-compatibility*"],"notes_tr":"n","notes_en":"n"},
          {"id":"rdp","group":"g","category":"network","container":true,"tr":"Uzak Masaüstü","en":"Remote Desktop"},
          {"id":"rdp-client","group":"g","category":"network","parent":"rdp","tr":"İstemci","en":"Client",
           "store":["microsoft-windows-terminalservices-clientactivexcore*"],"notes_tr":"n","notes_en":"n"},
          {"id":"rdp-core","group":"g","category":"network","parent":"rdp","locked":true,"tr":"Çekirdek","en":"Core",
           "store":["microsoft-windows-terminalservices-*"],"notes_tr":"n","notes_en":"n"},
          {"id":"lost","group":"g","category":"api","parent":"nowhere","tr":"Yetim","en":"Orphan",
           "store":["microsoft-windows-orphan*"],"notes_tr":"n","notes_en":"n"}]})");
    REQUIRE(catalog);
    REQUIRE(catalog->components().size() == 6);
    CHECK(catalog->find("lost")->parent.empty()); // a parent that is not there is dropped
    CHECK(catalog->find("rdp")->container);
    CHECK(catalog->find("ts")->recipe.services == std::vector<std::wstring>{L"Schedule"});

    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ComponentController controller(state, shippedCatalog(), Language::Turkish, [](std::function<void()> fn) { fn(); },
                                   std::move(*catalog));
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    AppState::SystemComponents system{L"C:\\m", {}};
    system.store["ts"] = {{L"microsoft-windows-taskscheduler-service"}, {L"microsoft-windows-taskscheduler-service"}, 100, 10};
    system.store["ts-compat"] = {{L"microsoft-windows-taskscheduler-compatibility"}, {}, 50, 0};
    system.store["rdp-client"] = {{L"microsoft-windows-terminalservices-clientactivexcore"}, {}, 20, 0};
    system.store["rdp-core"] = {{L"microsoft-windows-terminalservices-publicapis"}, {}, 30, 0};
    state.setSystemComponents(std::move(system));
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\m", {}, {}});

    const auto groups = controller.groups();
    REQUIRE(groups.size() == 2);
    const auto find = [&](const std::wstring& key) -> const ComponentController::Item& {
        for (const auto& g : groups) {
            for (const auto& item : g.items) {
                if (item.packageName == key) {
                    return item;
                }
            }
        }
        FAIL("no item " << utf8::fromWide(key));
        throw 0;
    };
    const auto& ts = find(L"ts");
    CHECK(ts.parentKey.empty());
    CHECK(ts.size == 100);
    CHECK(ts.recipe.storeNames == std::vector<std::wstring>{L"microsoft-windows-taskscheduler-service"});
    CHECK(ts.recipe.storeBits == 64);
    CHECK(ts.recipe.services == std::vector<std::wstring>{L"Schedule"});
    CHECK(ts.recipe.afterUpdates);
    const auto& ts32 = find(L"ts:32");
    CHECK(ts32.name == L"Görev Zamanlayıcısı - 32 bit");
    CHECK(ts32.parentKey == L"ts");
    CHECK(ts32.size == 10);
    CHECK(ts32.recipe.storeBits == 32);
    CHECK(ts32.recipe.services.empty()); // the service goes with the native row
    CHECK(find(L"ts-compat").parentKey == L"ts");
    CHECK(find(L"ts-compat").size == 50);
    // A container is a node over its entries; a locked entry is there, never removable.
    CHECK(find(L"rdp-client").path == std::vector<std::wstring>{L"Uzak Masaüstü"});
    CHECK(find(L"rdp-client").parentKey.empty());
    CHECK(find(L"rdp-core").lockReason == L"Gerekli");
    CHECK(std::ranges::none_of(groups[1].items, [](const auto& i) { return i.packageName == L"rdp"; }));

    // Removing a component takes what depends on it; keeping one again brings back what it needs.
    controller.toggle(ts);
    CHECK(controller.queued(ts));
    CHECK(controller.queued(ts32));
    CHECK(controller.queued(find(L"ts-compat")));
    const auto* op = state.changes().find(OpKind::RemoveComponent, L"ts");
    REQUIRE(op);
    const auto recipe = core::componentRecipeFromJson(utf8::fromWide(op->value));
    REQUIRE(recipe);
    CHECK(recipe->storeNames == std::vector<std::wstring>{L"microsoft-windows-taskscheduler-service"});
    CHECK(recipe->title == L"Görev Zamanlayıcısı");
    controller.toggle(find(L"ts-compat"));
    CHECK_FALSE(controller.queued(find(L"ts-compat")));
    CHECK_FALSE(controller.queued(ts));
    CHECK(controller.queued(ts32)); // not something the compatibility part needs
    controller.toggle(find(L"rdp-core"));
    CHECK_FALSE(controller.queued(find(L"rdp-core")));

    // A preset's store recipe gets this image's names back (D-076 for D-113 rows).
    ChangeSet saved;
    core::ops::Operation old{OpKind::RemoveComponent, L"ts-compat", L"{\"title\":\"x\",\"storeNames\":[\"stale\"]}"};
    saved.add(old);
    const auto current = controller.withCurrentRecipes(saved);
    const auto* mapped = current.find(OpKind::RemoveComponent, L"ts-compat");
    REQUIRE(mapped);
    CHECK(core::componentRecipeFromJson(utf8::fromWide(mapped->value))->storeNames ==
          std::vector<std::wstring>{L"microsoft-windows-taskscheduler-compatibility"});
}
