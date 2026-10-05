// D-045: P11 / P12 show what the mounted image already has. The image side is faked through
// AppState::setImageValues / setServiceList; the reader itself is tested in core/RegistryReadTests.
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/ImageValuesController.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"image-values";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string shipped(const wchar_t* file) {
    return readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog" / file);
}

const ImageSetting& setting(const ImageSettingsCatalog& catalog, std::string_view id) {
    const auto it = std::ranges::find(catalog.settings(), id, &ImageSetting::id);
    REQUIRE(it != catalog.settings().end());
    return *it;
}

int option(const ImageSetting& s, std::string_view id) {
    const auto it = std::ranges::find(s.options, id, &ImageSettingOption::id);
    REQUIRE(it != s.options.end());
    return static_cast<int>(it - s.options.begin());
}

const std::filesystem::path kMount = L"C:\\m";

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ImageSettingsController settings{state, *ImageSettingsCatalog::parse(shipped(L"settings.json"))};
    Fixture() { state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"}); }

    // The image has every operation of `ops` (registry values and files).
    void imageHas(const std::vector<Operation>& ops) {
        AppState::ImageValues values{AppState::ImageValues::Status::Ready, kMount, {}, {}, {}};
        if (const auto& old = state.imageValues()) {
            values.held = old->held;
        }
        for (const auto& op : ops) {
            values.held.insert(AppState::imageValueKey(op.kind, op.target, op.value));
        }
        state.setImageValues(std::move(values));
    }
    [[nodiscard]] const Operation* queued(OpKind kind, std::wstring_view target) const {
        return state.changes().find(kind, target);
    }
};

const wchar_t* const kTelemetry = L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection::AllowTelemetry";

} // namespace

TEST_CASE("image values: probes cover the settings catalog, text settings are read as strings") {
    const auto settings = ImageSettingsCatalog::parse(shipped(L"settings.json"));
    REQUIRE(settings);
    const auto probes = ImageValueProbes::from(*settings);
    CHECK(probes.writes.size() > 200);
    CHECK(probes.files.size() >= 1); // the taskbar layout (the Start one moved to the Başlat menüsü page, D-069)
    CHECK(probes.texts.size() >= 5); // OEM information
    // A text setting's listed write only names the value: it is never probed as a write.
    for (const auto& w : probes.writes) {
        CHECK(w.name != L"Manufacturer");
    }
}

TEST_CASE("image values: a setting the image already has shows as it is, and can be taken back") {
    Fixture f;
    const auto& telemetry = setting(f.settings.catalog(), "telemetry");
    const int security = option(telemetry, "security");
    CHECK(f.settings.current(telemetry) == telemetry.defaultOption);

    f.imageHas(ImageSettingsController::operationsFor(telemetry, security));
    CHECK(f.settings.imageOption(telemetry) == security);
    CHECK(f.settings.current(telemetry) == security);
    CHECK(f.settings.changedCount() == 0); // nothing for Uygula to do
    CHECK(f.state.changes().empty());

    // Back to the Windows default: the value is deleted.
    f.settings.select(telemetry, telemetry.defaultOption);
    const auto* revert = f.queued(OpKind::SetRegistryValue, kTelemetry);
    REQUIRE(revert);
    CHECK(revert->value == L"-");
    CHECK(f.settings.current(telemetry) == telemetry.defaultOption);
    CHECK(f.settings.changedCount() == 1);

    // And to what the image has: nothing queued.
    f.settings.select(telemetry, security);
    CHECK(f.state.changes().empty());
    CHECK(f.settings.current(telemetry) == security);

    // Another option: its own value in the same slot.
    const int required = option(telemetry, "required");
    f.settings.select(telemetry, required);
    REQUIRE(f.queued(OpKind::SetRegistryValue, kTelemetry));
    CHECK(f.queued(OpKind::SetRegistryValue, kTelemetry)->value == L"dword:00000001");
    CHECK(f.settings.current(telemetry) == required);
}

TEST_CASE("image values: an option of deletions only is not recognised; services have no way back") {
    Fixture f;
    const auto& share = setting(f.settings.catalog(), "context-share");
    f.imageHas(ImageSettingsController::operationsFor(share, option(share, "off")));
    CHECK(f.settings.imageOption(share) == share.defaultOption); // a missing key says nothing

    const auto& diagtrack = setting(f.settings.catalog(), "diagtrack-service");
    core::ServiceEntry service;
    service.name = L"DiagTrack";
    service.start = core::StartType::Disabled;
    f.state.setServiceList(AppState::ServiceList{AppState::ServiceList::Status::Ready, kMount, {service}, {}});
    const int off = option(diagtrack, "off");
    CHECK(f.settings.imageOption(diagtrack) == off);
    CHECK(f.settings.current(diagtrack) == off);
    CHECK(f.settings.revertOperations(diagtrack).empty());
    f.settings.select(diagtrack, diagtrack.defaultOption); // refused: the start type before is unknown
    CHECK(f.state.changes().empty());
    CHECK(f.settings.current(diagtrack) == off);
}

TEST_CASE("image values: values of another mount or still loading count for nothing") {
    Fixture f;
    const auto& telemetry = setting(f.settings.catalog(), "telemetry");
    const auto ops = ImageSettingsController::operationsFor(telemetry, option(telemetry, "security"));
    AppState::ImageValues values{AppState::ImageValues::Status::Loading, kMount, {}, {}, {}};
    for (const auto& op : ops) {
        values.held.insert(AppState::imageValueKey(op.kind, op.target, op.value));
    }
    f.state.setImageValues(values);
    CHECK(f.settings.imageOption(telemetry) == telemetry.defaultOption);
    values.status = AppState::ImageValues::Status::Ready;
    values.mountDir = L"C:\\other";
    f.state.setImageValues(values);
    CHECK(f.settings.imageOption(telemetry) == telemetry.defaultOption);
    // A new mount drops them.
    values.mountDir = kMount;
    f.state.setImageValues(values);
    CHECK(f.settings.imageOption(telemetry) != telemetry.defaultOption);
    f.state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 2, L"Home"});
    CHECK_FALSE(f.state.imageValues().has_value());
}

TEST_CASE("image values: text settings show the image's string") {
    Fixture f;
    const auto& manufacturer = setting(f.settings.catalog(), "oem-manufacturer");
    CHECK(f.settings.imageValue(manufacturer).empty());
    AppState::ImageValues values{AppState::ImageValues::Status::Ready, kMount, {}, {}, {}};
    values.texts[L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OEMInformation::Manufacturer"] = L"Contoso";
    f.state.setImageValues(std::move(values));
    CHECK(f.settings.imageValue(manufacturer) == L"Contoso");
    CHECK(f.settings.value(manufacturer).empty()); // the queue still has nothing
}

TEST_CASE("image values: the controller reads once per mount and ignores a stale answer") {
    AppState state{scratch(L"recent2.json"), scratch(L"settings2.json")};
    int reads = 0;
    std::vector<std::function<void()>> posted;
    ImageValuesController controller(
        state, ImageValueProbes{}, [&](std::function<void()> f) { posted.push_back(std::move(f)); },
        [&](const std::filesystem::path& mountDir, const ImageValueProbes&) -> Result<AppState::ImageValues> {
            ++reads;
            AppState::ImageValues v{AppState::ImageValues::Status::Ready, mountDir, {L"x"}, {}, {}};
            return v;
        });
    controller.load(); // nothing mounted
    CHECK(reads == 0);
    state.setMounted(MountedImage{kMount, L"C:\\w\\install.wim", 1, L"Pro"});
    controller.load();
    REQUIRE(state.imageValues());
    CHECK(state.imageValues()->status == AppState::ImageValues::Status::Loading);
    controller.load(); // already loading
    state.engine().drain();
    CHECK(reads == 1);
    for (auto& p : posted) {
        p();
    }
    posted.clear();
    REQUIRE(state.imageValues());
    CHECK(state.imageValues()->status == AppState::ImageValues::Status::Ready);
    CHECK(state.imageValues()->held.contains(L"x"));
    controller.load(); // there already
    state.engine().drain();
    CHECK(reads == 1);

    // Unmounted before the answer arrived: dropped.
    controller.load(/*force=*/true);
    state.engine().drain();
    state.setMounted(std::nullopt);
    for (auto& p : posted) {
        p();
    }
    CHECK_FALSE(state.imageValues().has_value());
}
