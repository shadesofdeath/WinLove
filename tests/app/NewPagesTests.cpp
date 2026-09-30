// D-048 / D-049 / D-051: the logic behind the Görevler, Hosts and Dosyalar pages.
#include "app/controllers/AppsController.h"
#include "app/controllers/FilesController.h"
#include "app/controllers/HostsController.h"
#include "app/controllers/ImageDriverController.h"
#include "app/controllers/LanguageController.h"
#include "app/controllers/TaskController.h"

#include <doctest.h>
#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;

namespace {

std::string shipped(const wchar_t* file) {
    std::ifstream in(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog" / file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"new-pages";
    std::filesystem::create_directories(dir);
    return dir / name;
}

const std::filesystem::path kMount = L"C:\\m";

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    Fixture() { state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"}); }
    void image(std::vector<std::wstring> tasks, std::map<std::wstring, std::wstring> hosts) {
        AppState::ImageValues v{AppState::ImageValues::Status::Ready, kMount, {}, {}, {}};
        for (const auto& t : tasks) {
            v.held.insert(AppState::imageValueKey(OpKind::SetTaskState, t, L"disabled"));
        }
        for (const auto& [id, entries] : hosts) {
            v.held.insert(AppState::imageValueKey(OpKind::SetHosts, id, entries));
        }
        v.disabledTasks = std::move(tasks);
        v.hostsSections = std::move(hosts);
        state.setImageValues(std::move(v));
    }
};

} // namespace

TEST_CASE("tasks page: the shipped catalog, toggling, the image's script, custom paths") {
    std::vector<TaskCategory> cats;
    const auto catalog = TaskController::parseCatalog(shipped(L"tasks.json"), &cats);
    const auto raw = nlohmann::json::parse(shipped(L"tasks.json"));
    CHECK(catalog.size() == raw["tasks"].size()); // nothing skipped as malformed
    CHECK(cats.size() == 4);

    Fixture f;
    TaskController tasks(f.state, shipped(L"tasks.json"));
    const auto appraiser = tasks.tasks().front(); // tasks() returns a copy
    CHECK_FALSE(tasks.off(appraiser.path));
    tasks.toggle(appraiser);
    CHECK(tasks.off(appraiser.path));
    REQUIRE(f.state.changes().find(OpKind::SetTaskState, appraiser.path));
    tasks.toggle(appraiser);
    CHECK(f.state.changes().empty());

    // The image already switches it off: shown off, turning it on queues "enabled".
    f.image({L"\\MICROSOFT\\Windows\\Application Experience\\Microsoft Compatibility Appraiser"}, {});
    CHECK(tasks.inImage(appraiser.path)); // case-insensitive
    CHECK(tasks.off(appraiser.path));
    tasks.toggle(appraiser);
    CHECK_FALSE(tasks.off(appraiser.path));
    CHECK(f.state.changes().find(OpKind::SetTaskState, appraiser.path)->value == L"enabled");
    tasks.toggle(appraiser);
    CHECK(f.state.changes().empty());

    const int recommended = tasks.applyRecommended();
    CHECK(recommended > 10);
    CHECK(tasks.applyRecommended() == 0);

    CHECK(tasks.addCustom(L"Microsoft\\Office\\OfficeTelemetryAgentLogOn\\"));
    CHECK(tasks.off(L"\\Microsoft\\Office\\OfficeTelemetryAgentLogOn"));
    CHECK(tasks.tasks().back().category == "custom");
    CHECK_FALSE(tasks.addCustom(L"\\bad\" & x"));
}

TEST_CASE("hosts page: lists on and off, the image's sections, imported entries") {
    const auto lists = HostsController::parseCatalog(shipped(L"hosts.json"));
    REQUIRE(lists.size() == 3);
    CHECK(lists[0].entries.size() > 20);

    Fixture f;
    HostsController hosts(f.state, shipped(L"hosts.json"));
    const auto& telemetry = hosts.lists()[0];
    hosts.toggle(telemetry);
    CHECK(hosts.on(telemetry));
    CHECK(hosts.totalEntries() == telemetry.entries.size());
    hosts.toggle(telemetry);
    CHECK(f.state.changes().empty()); // off again, and the image never had it: nothing queued

    // In the image: on without a queue; off queues the removal.
    f.image({}, {{telemetry.id, telemetry.text()}});
    CHECK(hosts.inImage(telemetry));
    CHECK(hosts.on(telemetry));
    hosts.toggle(telemetry);
    CHECK_FALSE(hosts.on(telemetry));
    CHECK(f.state.changes().find(OpKind::SetHosts, telemetry.id)->value.empty());
    hosts.toggle(telemetry);
    CHECK(f.state.changes().empty());

    CHECK(hosts.importText(L"0.0.0.0 a.example.com\n0.0.0.0 b.example.com\n127.0.0.1 localhost\n") == 2);
    CHECK(hosts.importText(L"0.0.0.0 b.example.com\n0.0.0.0 c.example.com\n") == 1);
    CHECK(hosts.customEntries().size() == 3);
    hosts.clearCustom();
    CHECK(hosts.customEntries().empty());
    CHECK(f.state.changes().find(OpKind::SetHosts, HostsController::kCustom) == nullptr);
}

TEST_CASE("files page: the typed place, the operation, refusals") {
    CHECK(FilesController::imageFolderFrom(L"C:\\Tools\\") == L"Tools");
    CHECK(FilesController::imageFolderFrom(L"c:/users/public/desktop") == L"users\\public\\desktop");
    CHECK(FilesController::imageFolderFrom(L"C:\\") == L"");
    CHECK(FilesController::imageFolderFrom(L"D:\\Data") == L"?");
    CHECK(FilesController::displayPath(L"Tools\\x") == L"C:\\Tools\\x");

    const auto source = scratch(L"payload");
    std::filesystem::create_directories(source);
    std::ofstream(source / L"a.txt") << "abc";
    auto op = FilesController::operationFor(source, L"Tools");
    REQUIRE(op);
    CHECK(op->kind == OpKind::CopyTree);
    CHECK(op->target == L"Tools\\payload");
    CHECK(op->sizeDelta == 3);
    CHECK(op->risk == core::ops::Risk::Low);
    auto risky = FilesController::operationFor(source, L"Windows\\Web");
    REQUIRE(risky);
    CHECK(risky->risk == core::ops::Risk::High);
    CHECK_FALSE(FilesController::operationFor(source, L"Windows\\System32\\config"));
    CHECK_FALSE(FilesController::operationFor(scratch(L"missing"), L"Tools"));

    Fixture f;
    FilesController files(f.state);
    const auto [n, errors] = files.add({source, scratch(L"missing")}, L"Tools");
    CHECK(n == 1);
    CHECK(errors.size() == 1);
    CHECK(files.count() == 1);
}

TEST_CASE("drivers page: removing a driver of the image") {
    Fixture f;
    ImageDriverController drivers(f.state, ImageDriverController::Events{});
    core::DriverEntry storage;
    storage.publishedName = L"oem1.inf";
    storage.originalFileName = L"iastorvd.inf";
    storage.bootCritical = true;
    core::DriverEntry net;
    net.publishedName = L"oem0.inf";
    net.originalFileName = L"rt640x64.inf";
    CHECK(ImageDriverController::operationFor(storage).risk == core::ops::Risk::High);
    CHECK(ImageDriverController::operationFor(net).risk == core::ops::Risk::Medium);
    drivers.toggle(net);
    CHECK(drivers.queuedForRemoval(net));
    CHECK(f.state.changes().find(OpKind::RemoveDriver, L"oem0.inf")->value == L"rt640x64.inf");
    CHECK(drivers.removalCount() == 1);
    drivers.toggle(net);
    CHECK(f.state.changes().empty());
    CHECK(drivers.hostFolder().filename() == L"host-drivers");
}

TEST_CASE("apps page: default associations merge, the browser pick, removing rows; app operations") {
    Fixture f;
    AppsController apps(f.state, AppsController::Events{});
    CHECK(apps.browser() == -1);
    apps.setBrowser(0); // Chrome
    CHECK(apps.browser() == 0);
    CHECK(apps.associations().size() == 4);
    apps.mergeAssociations({{L".pdf", L"SumatraPDF", L"SumatraPDF"}, {L"HTTP", L"MSEdgeHTM", L"Microsoft Edge"}});
    CHECK(apps.associations().size() == 5); // http replaced (identifiers without case), .pdf added
    CHECK(apps.browser() == 3);             // Edge now owns http
    apps.setBrowser(1);                     // Firefox: pages and links have their own ProgIds
    bool htmlIsPage = false;
    for (const auto& a : apps.associations()) {
        if (a.identifier == L".html") {
            htmlIsPage = a.progId == L"FirefoxHTML-308046B0AF4A39CB";
        }
    }
    CHECK(htmlIsPage);
    apps.setBrowser(-1);
    CHECK(apps.associations().size() == 1);
    apps.removeAssociation(L".PDF");
    CHECK(apps.associations().empty());
    CHECK(f.state.changes().empty()); // no associations: nothing queued

    core::AppxInstall framework;
    framework.package = LR"(C:\x\Microsoft.UI.Xaml.msix)";
    framework.framework = true;
    CHECK(AppsController::operationFor(framework).risk == core::ops::Risk::Low);
    core::AppxInstall app;
    app.package = LR"(C:\x\App.msix)";
    const auto op = AppsController::operationFor(app);
    CHECK(op.kind == OpKind::AddAppx);
    CHECK(op.risk == core::ops::Risk::Medium);
    f.state.queue(op);
    REQUIRE(apps.queuedApps().size() == 1);
    CHECK(apps.queuedApps().front().package == app.package);
    CHECK(apps.imageArchitecture() == L"x64");
}

TEST_CASE("languages page: packs of the image's architecture, the display languages, the queued settings") {
    Fixture f;
    LanguageController languages(f.state, [](std::function<void()> fn) { fn(); });
    std::vector<core::LanguagePackFile> found;
    for (const wchar_t* name : {LR"(D:\m\Microsoft-Windows-Client-Language-Pack_x64_de-de.cab)",
                                LR"(D:\m\Microsoft-Windows-Client-Language-Pack_arm64_de-de.cab)",
                                LR"(D:\m\Microsoft-Windows-LanguageFeatures-Basic-de-de-Package~31bf3856ad364e35~amd64~~.cab)"}) {
        found.push_back(core::classifyLanguageFile(name));
    }
    const auto fitting = languages.fitting(found); // no source open: x64
    REQUIRE(fitting.size() == 2);
    languages.queuePacks(fitting);
    languages.queuePacks(fitting); // once
    CHECK(languages.queuedPacks().size() == 2);
    CHECK(f.state.changes().find(OpKind::AddPackage, fitting[0].path.wstring())->value == L"language");
    CHECK(languages.changedCount() == 2);

    // The image has tr-TR; the queued pack adds de-DE.
    core::ImageIntl intl;
    intl.languages = {L"tr-TR"};
    f.state.setImageIntl(AppState::ImageIntl{AppState::ImageIntl::Status::Ready, kMount, intl, {}});
    CHECK(languages.uiLanguages() == std::vector<std::wstring>{L"tr-TR", L"de-DE"});

    core::IntlSettings s;
    s.uiLanguage = L"de-DE";
    s.timeZone = L"W. Europe Standard Time";
    languages.setSettings(s);
    CHECK(languages.settings() == s);
    CHECK(languages.changedCount() == 3);
    languages.setSettings(core::IntlSettings{});
    CHECK(f.state.changes().find(OpKind::SetIntl, L"intl") == nullptr);
    languages.unqueuePack(fitting[0].path);
    CHECK(languages.queuedPacks().size() == 1);

    // This PC's lists are there and sorted.
    CHECK(LanguageController::locales().size() > 100);
    CHECK(LanguageController::timeZones().size() > 50);
    CHECK(!LanguageController::keyboards().empty());
    CHECK(LanguageController::localeName(L"tr-TR") != L"tr-TR");
}
