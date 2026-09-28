// P07: the AppX catalog (parse, longest-prefix lookup, fallback group) and the component
// controller (grouping, tri-state group check, queue operations).
#include "app/catalog/AppxCatalog.h"
#include "app/controllers/ComponentController.h"

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
    controller.toggleGroup(xbox); // all → none
    CHECK(controller.queuedCount() == 0);

    const auto& system = groups[1];
    controller.toggle(system.items.front());
    CHECK(state.changes().find(OpKind::RemoveAppx, system.items.front().packageName)->risk == core::ops::Risk::High);
    controller.resetChanges();
    CHECK(state.changes().empty());
}
