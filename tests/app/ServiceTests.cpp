// P10: start-type keys, offline resource paths, dependents, and choice → ChangeSet mapping.
#include "app/controllers/ServiceController.h"

#include <doctest.h>

#include <filesystem>

using namespace wl;
using namespace wl::app;
using core::ServiceEntry;
using core::StartType;
using core::ops::OpKind;
using core::ops::Risk;

namespace {

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"services";
    std::filesystem::create_directories(dir);
    return dir / name;
}

ServiceEntry service(std::wstring name, StartType start, std::vector<std::wstring> deps = {}) {
    ServiceEntry s;
    s.name = name;
    s.displayName = name;
    s.start = start;
    s.type = 0x10;
    s.dependsOn = std::move(deps);
    return s;
}

constexpr std::string_view kCatalog = R"({"format":"winlove.catalog.services","services":[
    {"name":"DiagTrack","risk":"low","notes_tr":"Telemetri","notes_en":"Telemetry"},
    {"name":"wuauserv","risk":"high"}]})";

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ServiceController controller{state, kCatalog, [](std::function<void()> fn) { fn(); }};

    Fixture() {
        state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
        state.setServiceList(AppState::ServiceList{
            AppState::ServiceList::Status::Ready, L"C:\\m",
            {service(L"DiagTrack", StartType::Auto), service(L"wuauserv", StartType::Manual, {L"rpcss"}),
             service(L"Spooler", StartType::Auto), service(L"PrintNotify", StartType::Manual, {L"SPOOLER"}),
             service(L"Fax", StartType::Manual, {L"PrintNotify"})},
            {}});
    }
    const ServiceEntry& at(std::size_t i) const { return state.serviceList()->items[i]; }
};

} // namespace

TEST_CASE("startTypeKey round-trips every start type") {
    for (const auto t : {StartType::Boot, StartType::System, StartType::Auto, StartType::AutoDelayed,
                         StartType::Manual, StartType::Disabled}) {
        CHECK(core::startTypeFromKey(core::startTypeKey(t)) == t);
    }
    CHECK_FALSE(core::startTypeFromKey(L"sometimes"));
}

TEST_CASE("offlineResourcePath maps environment variables into the mount") {
    CHECK(core::offlineResourcePath(L"@%SystemRoot%\\system32\\DiagSvc.dll,-101", L"C:\\m") ==
          L"@C:\\m\\Windows\\system32\\DiagSvc.dll,-101");
    CHECK(core::offlineResourcePath(L"@%windir%\\x.dll,-1", L"C:\\m\\") == L"@C:\\m\\Windows\\x.dll,-1");
    CHECK(core::offlineResourcePath(L"@system32\\y.dll,-2", L"C:\\m") == L"@C:\\m\\Windows\\system32\\y.dll,-2");
    CHECK(core::offlineResourcePath(L"Plain Name", L"C:\\m") == L"Plain Name");
}

TEST_CASE("dependentsOf is case-insensitive and transitive") {
    Fixture f;
    const auto deps = core::dependentsOf(f.state.serviceList()->items, L"spooler");
    REQUIRE(deps.size() == 2);
    CHECK(deps[0] == L"PrintNotify");
    CHECK(deps[1] == L"Fax");
}

TEST_CASE("choosing a start type queues SetServiceStart; choosing the default removes it") {
    Fixture f;
    f.controller.set(f.at(0), StartType::Disabled);
    const auto* op = f.state.changes().find(OpKind::SetServiceStart, L"DiagTrack");
    REQUIRE(op);
    CHECK(op->value == L"disabled");
    CHECK(op->risk == Risk::Low);
    CHECK(f.controller.target(f.at(0)) == StartType::Disabled);
    CHECK(f.controller.changed(f.at(0)));

    f.controller.set(f.at(0), StartType::Auto);
    CHECK_FALSE(f.controller.changed(f.at(0)));
    CHECK(f.controller.queuedCount() == 0);
}

TEST_CASE("risk comes from the catalog; unknown = medium; enabling is at most medium") {
    Fixture f;
    CHECK(f.controller.risk(f.at(0)) == Risk::Low);
    CHECK(f.controller.risk(f.at(1)) == Risk::High);
    CHECK(f.controller.risk(f.at(2)) == Risk::Medium);
    CHECK(f.controller.notes(f.at(0), Language::English) == L"Telemetry");
    CHECK(ServiceController::operationFor(f.at(1), StartType::Disabled, Risk::High).risk == Risk::High);
    CHECK(ServiceController::operationFor(f.at(1), StartType::Auto, Risk::High).risk == Risk::Medium);
}

TEST_CASE("activeDependents lists dependents that stay enabled; reset clears service changes only") {
    Fixture f;
    CHECK(f.controller.activeDependents(f.at(2)).size() == 2);
    f.controller.set(f.at(3), StartType::Disabled); // PrintNotify off → Fax still depends transitively
    CHECK(f.controller.activeDependents(f.at(2)) == std::vector<std::wstring>{L"Fax"});
    f.state.queue(core::ops::Operation{OpKind::AddDriver, L"C:\\d\\a.inf", L"Net"});
    f.controller.resetChanges();
    CHECK(f.controller.queuedCount() == 0);
    CHECK(f.state.changes().size() == 1);
}
