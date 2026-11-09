// D-082: the compatibility guards in the app — the shipped catalog, the controller (which guards are
// on, what a row is held by, what leaves the queue), and how Bileşenler and Servisler refuse.
#include "app/catalog/AppxCatalog.h"
#include "app/catalog/CompatCatalog.h"
#include "app/catalog/ComponentCatalog.h"
#include "app/controllers/CompatController.h"
#include "app/controllers/ComponentController.h"
#include "app/controllers/PostSetupController.h"
#include "app/controllers/ServiceController.h"

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
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"compat";
    std::filesystem::create_directories(dir);
    std::error_code ec;
    std::filesystem::remove(dir / name, ec);
    return dir / name;
}

std::filesystem::path source(const wchar_t* relative) {
    return std::filesystem::path(WL_SOURCE_DIR) / relative;
}

core::AppxComponent appx(const wchar_t* identity, std::vector<std::wstring> needs = {}) {
    core::AppxComponent c;
    c.package.displayName = identity;
    c.package.packageName = std::wstring(identity) + L"_1.0.0.0_x64__8wekyb3d8bbwe";
    c.size = 1024;
    c.needs = std::move(needs);
    return c;
}

const ComponentController::Item* itemOf(const std::vector<ComponentController::Group>& groups, std::wstring_view identity) {
    for (const auto& g : groups) {
        for (const auto& item : g.items) {
            if (item.identity == identity) {
                return &item;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("compat catalog: the shipped guards parse and name only what exists") {
    auto catalog = CompatCatalog::parse(readFile(source(L"resources/catalog/compat.json")));
    REQUIRE(catalog);
    CHECK(catalog->guards().size() == 31);
    // D-121: on like the reference tool's own defaults (Windows Security, file sharing, camera, the apps …).
    CHECK(catalog->defaults() == std::vector<std::wstring>{L"core", L"windows-update", L"store", L"printing", L"wireless",
                                                           L"audio", L"webview-apps", L"recovery", L"security", L"sharing",
                                                           L"camera", L"setup", L"modern-apps", L"shell-search", L"night-light",
                                                           L"activation", L"anticheat", L"sfc", L"upgrade", L"recommended",
                                                           L"app-battlenet", L"app-discord", L"app-ea-anticheat",
                                                           L"app-legacy-games", L"app-office", L"app-netflix", L"app-nvidia",
                                                           L"app-visual-studio"});
    CHECK(catalog->find(L"core")->rule.always);
    CHECK(catalog->find(L"app-office")->section == CompatSection::Apps);
    CHECK(catalog->find(L"recommended")->section == CompatSection::Windows);
    auto components = ComponentCatalog::parse(readFile(source(L"resources/catalog/components.json")));
    auto appx = AppxCatalog::parse(readFile(source(L"resources/catalog/appx.json")));
    REQUIRE(components);
    REQUIRE(appx);
    // The reference tool's "Recommended" protection: every red (not recommended) component is held, not a hand-picked few
    // (the VM test of 2026-11-08: the preset took tabletpc, softkeyboard and edge-update).
    const auto* recommended = [&]() -> const CompatCatalogEntry* {
        for (const auto& guard : catalog->guards()) {
            if (guard.rule.id == L"recommended") {
                return &guard;
            }
        }
        return nullptr;
    }();
    REQUIRE(recommended != nullptr);
    for (const auto& entry : components->components()) {
        if (entry.tier == Tier::NotRecommended) {
            CAPTURE(entry.id);
            CHECK(std::ranges::find(recommended->rule.components, utf8::toWide(entry.id)) != recommended->rule.components.end());
        }
    }
    // A component's 32-bit row goes with it; a capability is held by its name, whatever the version (D-120).
    {
        std::vector<core::ops::CompatGuard> active{recommended->rule};
        core::ops::ChangeSet queue;
        const core::ops::Operation op32{core::ops::OpKind::RemoveComponent, L"tabletpc:32", L"{}"};
        CHECK_FALSE(core::ops::compatBlock(op32, active, {}, queue).empty());
        core::ops::Operation webview{core::ops::OpKind::RemoveCapability, L"Edge.Webview2.Platform~~~~", L""};
        CHECK(core::ops::compatBlock(webview, active, {}, queue).guards == std::vector<std::wstring>{L"recommended"});
        core::ops::Operation vbs{core::ops::OpKind::RemoveCapability, L"VBSCRIPT~~~~", L""};
        CHECK_FALSE(core::ops::compatBlock(vbs, active, {}, queue).empty());
        core::ops::Operation notepad{core::ops::OpKind::RemoveCapability, L"Microsoft.Windows.Notepad.System~~~~0.0.1.0", L""};
        CHECK(core::ops::compatBlock(notepad, active, {}, queue).empty());
    }
    for (const auto& guard : catalog->guards()) {
        CAPTURE(guard.rule.id);
        CHECK_FALSE(guard.descriptionTr.empty());
        CHECK_FALSE(guard.descriptionEn.empty());
        for (const auto& id : guard.rule.components) {
            CAPTURE(id);
            // One row may be named: a component's 32-bit row, or a font family of the image (D-119).
            if (id.starts_with(L"font:")) {
                CHECK(id.size() > 5);
                continue;
            }
            const std::wstring base = id.ends_with(L":32") ? id.substr(0, id.size() - 3) : id;
            CHECK(components->find(utf8::fromWide(base)) != nullptr);
        }
        for (const auto& prefix : guard.rule.appx) {
            CAPTURE(prefix);
            CHECK(appx->find(prefix) != nullptr); // every app it keeps is one the Bileşenler page can list
        }
    }
    // D-121: a component whose recipe takes along a service a guard keeps is held with it (the reference tool
    // locks the component the service belongs to): Bluetooth and the hotspot by Wi-Fi and Bluetooth,
    // the Xbox services by Xbox, the RDP host by Remote Desktop, the SMB server, function discovery and
    // SSDP by Network sharing — none of them named by their guard. Not by "core": its service keys
    // stay anyway, it only refuses "disabled".
    {
        core::ops::ChangeSet queue;
        const auto removal = [&](const char* id) {
            const auto* entry = components->find(id);
            REQUIRE(entry != nullptr);
            return core::ops::Operation{core::ops::OpKind::RemoveComponent, utf8::toWide(id),
                                        utf8::toWide(core::componentRecipeToJson(entry->recipe))};
        };
        for (const auto& [guardId, componentId] : std::vector<std::pair<const wchar_t*, const char*>>{
                 {L"wireless", "bluetooth"}, {L"wireless", "hotspot"}, {L"xbox", "xbox"}, {L"remote-desktop", "rdp-host"},
                 {L"sharing", "smbserver"}, {L"sharing", "fdphost"}, {L"sharing", "ssdp"}}) {
            CAPTURE(std::wstring(guardId));
            const auto* guard = catalog->find(guardId);
            REQUIRE(guard != nullptr);
            CHECK(std::ranges::find(guard->rule.components, utf8::toWide(componentId)) == guard->rule.components.end());
            std::vector<core::ops::CompatGuard> active{guard->rule};
            CHECK(core::ops::compatBlock(removal(componentId), active, {}, queue).guards == std::vector<std::wstring>{guardId});
        }
        // A recipe's app counts like the app itself.
        core::ComponentRecipe withApp;
        withApp.title = L"x";
        withApp.appx = {L"Microsoft.WindowsStore_1.0.0.0_x64__8wekyb3d8bbwe"};
        const core::ops::Operation appRemoval{core::ops::OpKind::RemoveComponent, L"x",
                                              utf8::toWide(core::componentRecipeToJson(withApp))};
        std::vector<core::ops::CompatGuard> store{catalog->find(L"store")->rule};
        CHECK_FALSE(core::ops::compatBlock(appRemoval, store, {}, queue).empty());
        // "core" lets a removal through but never a disabled RpcSs or BitLocker's volume filter.
        core::ops::CompatGuard keeps = catalog->find(L"core")->rule;
        keeps.services.push_back(L"bthserv");
        std::vector<core::ops::CompatGuard> always{keeps};
        CHECK(core::ops::compatBlock(removal("bluetooth"), always, {}, queue).empty());
        for (const wchar_t* service : {L"RpcSs", L"fvevol", L"TokenBroker"}) {
            CAPTURE(std::wstring(service));
            const core::ops::Operation disable{core::ops::OpKind::SetServiceStart, service, L"disabled"};
            CHECK(core::ops::compatBlock(disable, always, {}, queue).guards == std::vector<std::wstring>{L"core"});
        }
        // One row by name: Visual Studio keeps 32-bit PowerShell, not PowerShell; Office keeps Gadugi.
        std::vector<core::ops::CompatGuard> vs{catalog->find(L"app-visual-studio")->rule};
        CHECK_FALSE(core::ops::compatBlock({core::ops::OpKind::RemoveComponent, L"powershell:32", L"{}"}, vs, {}, queue).empty());
        CHECK(core::ops::compatBlock({core::ops::OpKind::RemoveComponent, L"powershell", L"{}"}, vs, {}, queue).empty());
        std::vector<core::ops::CompatGuard> office{catalog->find(L"app-office")->rule};
        CHECK_FALSE(core::ops::compatBlock({core::ops::OpKind::RemoveComponent, L"font:Gadugi", L"{}"}, office, {}, queue).empty());
        CHECK(core::ops::compatBlock({core::ops::OpKind::RemoveComponent, L"font:Impact", L"{}"}, office, {}, queue).empty());
    }
    // A guard with nothing to keep, a repeated id or the wrong file is not a guard.
    const auto odd = CompatCatalog::parse(R"({"format":"winlove.catalog.compat","guards":[
        {"id":"a","tr":"A","en":"A"},
        {"id":"b","tr":"B","en":"B","services":["Spooler"]},
        {"id":"b","tr":"B2","en":"B2","services":["BITS"]}]})");
    REQUIRE(odd);
    REQUIRE(odd->guards().size() == 1);
    CHECK(odd->guards().front().nameTr == L"B");
    CHECK_FALSE(CompatCatalog::parse(R"({"format":"winlove.catalog.programs"})"));
}

TEST_CASE("compat controller: rows held by guards and kept apps; what leaves the queue and why") {
    const Localization strings = Localization::fromJson(readFile(source(L"resources/strings/tr.json"))).value();
    const auto settingsFile = scratch(L"settings.json");
    AppState state{scratch(L"recent.json"), settingsFile};
    auto appxCatalog = AppxCatalog::parse(readFile(source(L"resources/catalog/appx.json")));
    REQUIRE(appxCatalog);
    ComponentController components(state, *appxCatalog, Language::Turkish, [](std::function<void()> fn) { fn(); });
    PostSetupController postSetup(state);
    CompatController compat(state, strings, Language::Turkish,
                            CompatCatalog::parse(readFile(source(L"resources/catalog/compat.json"))).value(), components.catalog(),
                            postSetup);
    std::vector<std::wstring> dropped;
    compat.onDropped = [&](const std::wstring&, const std::wstring& body) { dropped.push_back(body); };
    components.blockOf = [&](const core::ops::Operation& op) { return compat.block(op); };
    int refused = 0;
    components.onBlocked = [&](const ComponentController::Item&, const core::ops::CompatBlock&) { ++refused; };

    // A new user: the catalog's defaults. "core" is on and not counted (not a choice).
    CHECK(compat.isOn(L"windows-update"));
    CHECK(compat.isOn(L"security")); // D-121: on, like the reference tool's
    CHECK_FALSE(compat.isOn(L"xbox"));
    CHECK(compat.isOn(L"core"));
    CHECK(compat.onCount() == 27);
    {
        const auto ids = compat.onIds();
        CHECK(std::ranges::find(ids, L"core") == ids.end());
    }
    CHECK(compat.isOn(L"recommended")); // D-110: the reference tool's "Önerilen" holds the red rows
    // D-120: settings saved before the setup guards existed ("guards": [], no "guardsSeen") chose
    // among the old ones only: those stay off, the new ones take their default.
    {
        AppSettings old = state.settings();
        old.guards = std::vector<std::wstring>{};
        old.guardsSeen.reset();
        state.setSettings(old);
        CHECK_FALSE(compat.isOn(L"recommended"));
        CHECK_FALSE(compat.isOn(L"store"));
        CHECK_FALSE(compat.isOn(L"security")); // seen then: its default changed later, the choice stays
        CHECK(compat.isOn(L"setup"));
        CHECK(compat.isOn(L"night-light"));
        CHECK(compat.isOn(L"activation")); // D-121
        CHECK(compat.isOn(L"app-office"));
        CHECK(compat.isOn(L"core"));
        compat.setOn(L"setup", false); // now chosen: it stays off
        CHECK_FALSE(compat.isOn(L"setup"));
        CHECK(state.settings().guardsSeen.has_value());
        const std::vector<std::wstring> saved = state.settings().guards.value_or(std::vector<std::wstring>{});
        CHECK(std::ranges::find(saved, L"core") == saved.end());
        compat.setOn(L"core", false); // cannot be switched off
        CHECK(compat.isOn(L"core"));
        compat.setGuards({});
        CHECK(compat.isOn(L"core"));
        CHECK(compat.onCount() == 0);
        compat.setOn(L"setup", true);
        AppSettings fresh = state.settings();
        fresh.guards.reset();
        fresh.guardsSeen.reset();
        state.setSettings(fresh);
        CHECK(compat.onCount() == 27);
    }

    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Ready, L"C:\\m",
                                         {appx(L"Microsoft.WindowsStore", {L"Microsoft.VCLibs.140.00"}),
                                          appx(L"Microsoft.WindowsCalculator", {L"Microsoft.VCLibs.140.00"}),
                                          appx(L"Microsoft.VCLibs.140.00"), appx(L"Microsoft.DesktopAppInstaller"),
                                          appx(L"Microsoft.BingWeather")},
                                         {}});
    const auto groups = components.groups();
    const auto* store = itemOf(groups, L"Microsoft.WindowsStore");
    const auto* calculator = itemOf(groups, L"Microsoft.WindowsCalculator");
    const auto* vclibs = itemOf(groups, L"Microsoft.VCLibs.140.00");
    const auto* installer = itemOf(groups, L"Microsoft.DesktopAppInstaller");
    REQUIRE(store);
    REQUIRE(calculator);
    REQUIRE(vclibs);
    REQUIRE(installer);

    // The Store guard holds the Store; a click is refused and says so.
    const auto storeBlock = components.block(*store);
    CHECK(storeBlock.guards == std::vector<std::wstring>{L"store", L"recommended"});
    CHECK(compat.explain(storeBlock) == L"Korunuyor: Microsoft Store ve uygulama kurulumu, Önerilen");
    components.toggle(*store);
    CHECK(refused == 1);
    CHECK(state.changes().empty());
    // VCLibs: the guard and two kept apps.
    const auto vclibsBlock = components.block(*vclibs);
    CHECK(vclibsBlock.neededBy.size() == 2);
    CHECK(compat.explain(vclibsBlock).find(L"Kullanan: Hesap Makinesi, Microsoft Store") != std::wstring::npos);

    // Without the Store guard, VCLibs goes once both apps go; Calculator taken back keeps it.
    compat.setGuards({L"windows-update", L"printing"});
    CHECK(AppSettings::load(settingsFile).guards == std::vector<std::wstring>{L"windows-update", L"printing"});
    CHECK(components.block(*vclibs).guards.empty());
    CHECK_FALSE(components.block(*vclibs).empty());
    components.toggle(*store);
    components.toggle(*calculator);
    CHECK(components.block(*vclibs).empty());
    components.toggle(*vclibs);
    CHECK(state.changes().size() == 3);
    components.toggle(*calculator); // back off the queue
    CHECK(state.changes().size() == 1); // VCLibs left with it
    REQUIRE(dropped.size() == 1);
    CHECK(dropped.front().find(L"Hesap Makinesi") != std::wstring::npos);

    // Programs on the Programlar page hold App Installer (their window installs with winget).
    components.toggle(*installer);
    CHECK(state.changes().find(OpKind::RemoveAppx, installer->packageName));
    postSetup.setPrograms({{L"Git.Git", L"Git"}}, {});
    CHECK_FALSE(state.changes().find(OpKind::RemoveAppx, installer->packageName));
    CHECK(dropped.size() == 2);
    CHECK(compat.explain(components.block(*installer)) == L"Korunuyor: Programlar sayfası (winget)");

    // A service a guard keeps is not disabled; switching a guard on takes such a change back.
    ServiceController services(state, R"({"format":"winlove.catalog.services","services":[]})", [](std::function<void()> fn) { fn(); });
    services.blockOf = [&](const core::ops::Operation& op) { return compat.block(op); };
    int servicesRefused = 0;
    services.onBlocked = [&](const core::ServiceEntry&, const core::ops::CompatBlock&) { ++servicesRefused; };
    core::ServiceEntry spooler;
    spooler.name = L"Spooler";
    spooler.start = core::StartType::Auto;
    core::ServiceEntry wlan;
    wlan.name = L"WlanSvc";
    wlan.start = core::StartType::Manual;
    CHECK(services.guarded(spooler)); // "printing" is on
    CHECK_FALSE(services.set(spooler, core::StartType::Disabled));
    CHECK(servicesRefused == 1);
    CHECK(services.set(spooler, core::StartType::Manual)); // only "disabled" is held
    CHECK_FALSE(services.guarded(wlan));
    CHECK(services.set(wlan, core::StartType::Disabled));
    compat.setOn(L"wireless", true);
    CHECK_FALSE(state.changes().find(OpKind::SetServiceStart, L"WlanSvc"));
    CHECK(state.changes().find(OpKind::SetServiceStart, L"Spooler"));
}

TEST_CASE("compat controller: the impact report names what the queue breaks and switching a guard on keeps it (D-124)") {
    const Localization strings = Localization::fromJson(readFile(source(L"resources/strings/tr.json"))).value();
    AppState state{scratch(L"recent-impact.json"), scratch(L"settings-impact.json")};
    auto appxCatalog = AppxCatalog::parse(readFile(source(L"resources/catalog/appx.json")));
    REQUIRE(appxCatalog);
    PostSetupController postSetup(state);
    CompatController compat(state, strings, Language::Turkish,
                            CompatCatalog::parse(readFile(source(L"resources/catalog/compat.json"))).value(), *appxCatalog, postSetup);
    compat.describe = [](const core::ops::Operation& op) { return op.target.substr(0, op.target.find(L'_')); };
    state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    CHECK(compat.impact().empty()); // nothing queued

    REQUIRE_FALSE(compat.isOn(L"xbox"));
    state.queue(core::ops::Operation{OpKind::RemoveAppx, L"Microsoft.XboxGamingOverlay_7.0.0.0_x64__8wekyb3d8bbwe", L""});
    state.queue(core::ops::Operation{OpKind::RemoveAppx, L"Microsoft.GamingApp_2.0.0.0_x64__8wekyb3d8bbwe", L""});
    state.queue(core::ops::Operation{OpKind::RemoveAppx, L"Microsoft.BingWeather_4.0.0.0_x64__8wekyb3d8bbwe", L""});
    const auto impact = compat.impact();
    REQUIRE(impact.size() == 1); // the weather app breaks no feature a guard knows
    CHECK(impact[0].guard == L"xbox");
    CHECK_FALSE(impact[0].app);
    CHECK_FALSE(impact[0].name.empty());
    CHECK(impact[0].items == std::vector<std::wstring>{L"Microsoft.XboxGamingOverlay", L"Microsoft.GamingApp"});

    compat.setOn(L"xbox", true); // "Korumayı aç": its steps leave the queue
    CHECK(compat.impact().empty());
    CHECK_FALSE(state.changes().find(OpKind::RemoveAppx, L"Microsoft.GamingApp_2.0.0.0_x64__8wekyb3d8bbwe"));
    CHECK(state.changes().find(OpKind::RemoveAppx, L"Microsoft.BingWeather_4.0.0.0_x64__8wekyb3d8bbwe"));
}
