// D-152: the pages' Sıfırla (Görevler, Kurulum Sonrası, Sürücüler, Kayıt Defteri, Güncellemeler; a tab
// of Ayarlar): each takes only its own changes out of the queue, as one undo step.
#include "app/controllers/ImageDriverController.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/PostSetupController.h"
#include "app/controllers/RegistryController.h"
#include "app/controllers/TaskController.h"
#include "app/pages/UpdatesPage.h"
#include "core/image/RegistryInput.h"

#include <doctest.h>

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
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"reset";
    std::filesystem::create_directories(dir);
    return dir / name;
}

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    Fixture() {
        state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
        state.queue(Operation{OpKind::DisableFeature, L"Printing-XPSServices-Features"}); // another page's
    }
    [[nodiscard]] bool otherStays() const {
        return state.changes().find(OpKind::DisableFeature, L"Printing-XPSServices-Features") != nullptr;
    }
};

} // namespace

TEST_CASE("reset: Görevler takes its task changes and created tasks out, nothing else") {
    Fixture f;
    TaskController tasks(f.state, shipped(L"tasks.json"));
    tasks.toggle(tasks.tasks().front());
    REQUIRE(tasks.addCustom(L"\\Microsoft\\Office\\OfficeTelemetryAgentLogOn"));
    f.state.queue(Operation{OpKind::CreateTask, L"tasks-create", L"[]"});
    REQUIRE(tasks.changedCount() == 3);
    tasks.resetChanges();
    CHECK(tasks.changedCount() == 0);
    CHECK(f.otherStays());
    f.state.undoQueue(); // one step brings all three back
    CHECK(tasks.changedCount() == 3);
}

TEST_CASE("reset: Kurulum Sonrası takes its steps out; the Programlar page's picks stay") {
    Fixture f;
    PostSetupController post{f.state};
    post.add(core::PostSetupStep{core::PostSetupStep::Type::Command, L"power", L"powercfg /h off", {}, true});
    post.add(core::PostSetupStep{core::PostSetupStep::Type::Command, L"dns", L"ipconfig /flushdns", {}, true});
    post.setPrograms({core::PostSetupProgram{L"Mozilla.Firefox", L"Firefox"}}, {});
    REQUIRE(post.stepCount() == 2);
    post.resetSteps();
    CHECK(post.stepCount() == 0);
    REQUIRE(post.programs().size() == 1);
    CHECK(post.programs().front().id == L"Mozilla.Firefox");
    CHECK(f.otherStays());

    // Without programs the plan is empty: its operation leaves the queue.
    post.setPrograms({}, {});
    post.add(core::PostSetupStep{core::PostSetupStep::Type::Command, L"power", L"powercfg /h off", {}, true});
    REQUIRE(f.state.changes().find(OpKind::SetPostSetup, PostSetupController::kTarget));
    post.resetSteps();
    CHECK(f.state.changes().find(OpKind::SetPostSetup, PostSetupController::kTarget) == nullptr);
    CHECK(f.otherStays());
}

TEST_CASE("reset: Sürücüler takes drivers to add and to remove out, and unticks boot.wim's") {
    Fixture f;
    ImageDriverController drivers(f.state, ImageDriverController::Events{});
    core::DriverEntry net;
    net.publishedName = L"oem0.inf";
    net.originalFileName = L"rt640x64.inf";
    drivers.toggle(net);
    f.state.queue(Operation{OpKind::AddDriver, L"C:\\d\\iaStorVD.inf", L"SCSIAdapter"});
    f.state.setBootDriver(L"C:\\d\\iaStorVD.inf", true);
    REQUIRE(f.state.bootDrivers().size() == 1);
    drivers.resetChanges();
    CHECK(f.state.changes().find(OpKind::RemoveDriver, L"oem0.inf") == nullptr);
    CHECK(f.state.changes().find(OpKind::AddDriver, L"C:\\d\\iaStorVD.inf") == nullptr);
    CHECK(f.state.bootDrivers().empty());
    CHECK(f.otherStays());
}

TEST_CASE("reset: Kayıt Defteri switches every entry off and keeps the list; Geri al switches them on") {
    Fixture f;
    RegistryController registry{f.state};
    REQUIRE(registry.addValue(*core::registryWriteFromInput(LR"(HKLM\SOFTWARE\Contoso)", L"Mode", core::RegValueType::Dword, L"0x10"),
                              /*afterSetup=*/false));
    REQUIRE(registry.addValue(*core::registryWriteFromInput(LR"(HKCU\Software\Contoso)", L"Name", core::RegValueType::String, L"x"),
                              /*afterSetup=*/true));
    REQUIRE(registry.checkedCount() == 2);
    registry.resetChanges();
    CHECK(registry.checkedCount() == 0);
    CHECK(f.state.regImports().size() == 2);
    CHECK(f.state.changes().size() == 1); // the other page's only
    f.state.undoQueue();
    CHECK(registry.checkedCount() == 2);
}

TEST_CASE("reset: Güncellemeler takes its packages out; the Diller page's language packs stay") {
    Fixture f;
    f.state.queue(Operation{OpKind::AddPackage, L"C:\\u\\windows11.0-kb1-x64.msu", L"lcu"});
    f.state.queue(Operation{OpKind::AddPackage, L"C:\\u\\ssu.cab", L"ssu"});
    f.state.queue(Operation{OpKind::AddPackage, L"C:\\u\\safeos.cab", L"safeos"});
    f.state.queue(Operation{OpKind::AddPackage, L"C:\\l\\tr-tr.cab", L"language"});
    UpdatesPage::resetQueued(f.state);
    CHECK(f.state.changes().count(OpKind::AddPackage) == 1);
    CHECK(f.state.changes().find(OpKind::AddPackage, L"C:\\l\\tr-tr.cab") != nullptr);
    CHECK(f.otherStays());
}

TEST_CASE("reset: an Ayarlar tab takes back its own settings only") {
    Fixture f;
    auto catalog = ImageSettingsCatalog::parse(shipped(L"settings.json"));
    REQUIRE(catalog);
    ImageSettingsController settings{f.state, std::move(*catalog)};
    const auto firstToggle = [&](std::string_view tab) -> const ImageSetting* {
        for (const ImageSetting* s : settings.settingsOfTab(tab)) {
            if (s->control == ImageSetting::Control::Toggle) {
                return s;
            }
        }
        return nullptr;
    };
    const ImageSetting* privacy = firstToggle("privacy");
    const ImageSetting* system = firstToggle("system");
    REQUIRE(privacy);
    REQUIRE(system);
    settings.select(*privacy, 1 - settings.current(*privacy));
    settings.select(*system, 1 - settings.current(*system));
    REQUIRE(settings.changedCount() == 2);

    CHECK(settings.resetSettings(settings.settingsOfTab("privacy")) == 1);
    CHECK(settings.current(*privacy) == settings.imageOption(*privacy));
    CHECK(settings.current(*system) != settings.imageOption(*system));
    CHECK(settings.changedCount() == 1);
    CHECK(f.otherStays());
    // Every tab's settings are some tab's: "Tümünü sıfırla" covers what the tabs do.
    std::size_t inTabs = 0;
    for (const auto& tab : settings.catalog().tabs()) {
        inTabs += settings.settingsOfTab(tab.id).size();
    }
    CHECK(inTabs == settings.catalog().settings().size());
    CHECK(settings.resetChanges() == 1);
    CHECK(settings.changedCount() == 0);
}
