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
    CHECK(catalog->guards().size() == 13);
    CHECK(catalog->defaults() == std::vector<std::wstring>{L"windows-update", L"store", L"printing", L"wireless", L"audio",
                                                           L"webview-apps", L"recovery"});
    auto components = ComponentCatalog::parse(readFile(source(L"resources/catalog/components.json")));
    auto appx = AppxCatalog::parse(readFile(source(L"resources/catalog/appx.json")));
    REQUIRE(components);
    REQUIRE(appx);
    for (const auto& guard : catalog->guards()) {
        CAPTURE(guard.rule.id);
        CHECK_FALSE(guard.descriptionTr.empty());
        CHECK_FALSE(guard.descriptionEn.empty());
        for (const auto& id : guard.rule.components) {
            CAPTURE(id);
            CHECK(components->find(utf8::fromWide(id)) != nullptr);
        }
        for (const auto& prefix : guard.rule.appx) {
            CAPTURE(prefix);
            CHECK(appx->find(prefix) != nullptr); // every app it keeps is one the Bileşenler page can list
        }
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

    // A new user: the catalog's defaults.
    CHECK(compat.isOn(L"windows-update"));
    CHECK_FALSE(compat.isOn(L"security"));
    CHECK(compat.onCount() == 7);

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
    CHECK(storeBlock.guards == std::vector<std::wstring>{L"store"});
    CHECK(compat.explain(storeBlock) == L"Korunuyor: Microsoft Store ve uygulama kurulumu");
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
