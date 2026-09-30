// P12: the settings catalog (shipped file, malformed entries) and the form ↔ queue mapping.
#include "app/catalog/TweakCatalog.h"
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/RegistryController.h"

#include <doctest.h>
#include <json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
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
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"image-settings";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string shippedJson(const wchar_t* file) {
    return readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog" / file);
}

ImageSettingsCatalog shippedCatalog() {
    auto catalog = ImageSettingsCatalog::parse(shippedJson(L"settings.json"));
    REQUIRE(catalog);
    return std::move(*catalog);
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

struct Fixture {
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    ImageSettingsController controller{state, shippedCatalog()};
    Fixture() { state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"}); }
};

} // namespace

TEST_CASE("settings catalog: every shipped entry is valid and placed in a known tab") {
    const auto catalog = shippedCatalog();
    // Nothing was skipped as malformed.
    const auto raw = nlohmann::json::parse(shippedJson(L"settings.json"));
    CHECK(catalog.settings().size() == raw["settings"].size());
    CHECK(catalog.tabs().size() == 6);

    std::set<std::string> ids;
    for (const auto& s : catalog.settings()) {
        CAPTURE(s.id);
        CHECK(ids.insert(s.id).second);
        CHECK_FALSE(s.label.tr.empty());
        CHECK_FALSE(s.label.en.empty());
        const auto section = std::ranges::find(catalog.sections(), s.section, &ImageSettingSection::id);
        REQUIRE(section != catalog.sections().end());
        CHECK(std::ranges::find(catalog.tabs(), section->tab, &ImageSettingTab::id) != catalog.tabs().end());
        CHECK(std::ranges::count_if(s.options, &ImageSettingOption::isDefault) == 1);
        CHECK(s.options[static_cast<std::size_t>(s.defaultOption)].isDefault());
        if (s.control == ImageSetting::Control::Toggle) {
            CHECK(s.options.size() == 2);
        } else {
            for (const auto& o : s.options) {
                CHECK_FALSE(o.label.tr.empty());
                CHECK_FALSE(o.label.en.empty());
            }
        }
        CHECK(s.recommended != s.defaultOption); // recommending "leave it" is no recommendation
    }
}

TEST_CASE("settings catalog: malformed settings are skipped, the rest loads") {
    const auto catalog = ImageSettingsCatalog::parse(R"({
      "format": "winlove.catalog.settings",
      "tabs": [ { "id": "t", "tr": "T", "en": "T" } ],
      "sections": [ { "id": "s", "tab": "t", "tr": "S", "en": "S" } ],
      "settings": [
        { "id": "ok", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "off",
          "writes": [ { "key": "HKCU\\Software\\X", "name": "v", "value": "dword:00000001" } ] },
        { "id": "no-default", "section": "s", "control": "toggle", "tr": "a", "en": "a",
          "writes": [ { "key": "HKCU\\Software\\X", "name": "v", "value": "dword:00000001" } ] },
        { "id": "no-writes", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "on" },
        { "id": "bad-value", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "on",
          "writes": [ { "key": "HKCU\\Software\\X", "name": "v", "value": "dword:xyz" } ] },
        { "id": "bad-service", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "on",
          "services": [ { "name": "X", "start": "sometimes" } ] },
        { "id": "two-defaults", "section": "s", "control": "radio", "tr": "a", "en": "a",
          "options": [ { "id": "a" }, { "id": "b" } ] },
        { "id": "lost", "section": "nowhere", "control": "toggle", "tr": "a", "en": "a", "default": "on",
          "writes": [ { "key": "HKCU\\Software\\X", "name": "v", "value": "dword:00000001" } ] }
      ] })");
    REQUIRE(catalog);
    REQUIRE(catalog->settings().size() == 1);
    CHECK(catalog->settings().front().id == "ok");
    CHECK(catalog->settings().front().defaultOption == 0);
    CHECK_FALSE(ImageSettingsCatalog::parse(R"({"format":"winlove.catalog.tweaks"})"));
    CHECK_FALSE(ImageSettingsCatalog::parse(R"({"format":"winlove.catalog.settings","settings":[{"id":5}]})"));
}

TEST_CASE("settings and registry tweaks that write the same values use the same operation kind") {
    // Otherwise the Registry page and this form would queue the same value twice, in two slots.
    const auto settings = shippedCatalog();
    const auto tweaks = TweakCatalog::parse(shippedJson(L"tweaks.json"));
    REQUIRE(tweaks);
    int shared = 0;
    for (const auto& tweak : tweaks->tweaks()) {
        for (const auto& s : settings.settings()) {
            for (const auto& o : s.options) {
                if (o.writes == tweak.writes && o.services.empty()) {
                    CAPTURE(tweak.id);
                    CHECK(s.firstLogon == tweak.firstLogon);
                    ++shared;
                }
            }
        }
    }
    CHECK(shared >= 25);
}

TEST_CASE("form: the selected option is read from the queue; default removes the operations") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& telemetry = setting(catalog, "telemetry");
    CHECK(f.controller.current(telemetry) == telemetry.defaultOption);
    CHECK(f.controller.changedCount() == 0);

    f.controller.select(telemetry, option(telemetry, "security"));
    REQUIRE(f.state.changes().size() == 1);
    CHECK(f.state.changes().operations().front().kind == OpKind::SetRegistryValue);
    CHECK(f.state.changes().operations().front().value == L"dword:00000000");
    CHECK(f.controller.current(telemetry) == option(telemetry, "security"));
    CHECK(f.controller.changedCount() == 1);

    // Another option of the same setting replaces the value, it does not add to it.
    f.controller.select(telemetry, option(telemetry, "required"));
    REQUIRE(f.state.changes().size() == 1);
    CHECK(f.state.changes().operations().front().value == L"dword:00000001");
    CHECK(f.controller.current(telemetry) == option(telemetry, "required"));

    f.controller.select(telemetry, telemetry.defaultOption);
    CHECK(f.state.changes().empty());
    CHECK(f.controller.changedCount() == 0);
}

TEST_CASE("form: first-logon settings, service settings and options on different keys") {
    Fixture f;
    const auto& catalog = f.controller.catalog();

    const auto& ads = setting(catalog, "advertising-id");
    f.controller.select(ads, option(ads, "off"));
    CHECK(f.state.changes().count(OpKind::SetRegistryFirstLogon) == 2);
    CHECK(f.controller.current(ads) == option(ads, "off"));

    const auto& indexing = setting(catalog, "search-indexing");
    f.controller.select(indexing, option(indexing, "off"));
    const auto* service = f.state.changes().find(OpKind::SetServiceStart, L"WSearch");
    REQUIRE(service);
    CHECK(service->value == L"disabled");
    CHECK(service->risk == core::ops::Risk::Medium);
    // The Services page setting the service to something else: the form no longer claims "off".
    f.state.queue(core::ops::Operation{OpKind::SetServiceStart, L"WSearch", L"manual"});
    CHECK(f.controller.current(indexing) == indexing.defaultOption);

    // Location: "off" writes HKLM, "system" writes HKCU — switching must not leave the other behind.
    const auto& location = setting(catalog, "location");
    f.controller.select(location, option(location, "off"));
    f.controller.select(location, option(location, "system"));
    CHECK(f.controller.current(location) == option(location, "system"));
    std::size_t locationOps = 0;
    for (const auto& op : f.state.changes().operations()) {
        locationOps += op.target.find(L"ConsentStore\\location") != std::wstring::npos ? 1 : 0;
    }
    CHECK(locationOps == 1);
}

TEST_CASE("form and Registry page share the queue: a tweak checked there shows here, and back") {
    Fixture f;
    auto tweaks = TweakCatalog::parse(shippedJson(L"tweaks.json"));
    REQUIRE(tweaks);
    RegistryController registry{f.state, std::move(*tweaks)};
    const auto& catalog = f.controller.catalog();

    const auto darkTweak = std::ranges::find(registry.catalog().tweaks(), std::string("dark-mode"), &Tweak::id);
    REQUIRE(darkTweak != registry.catalog().tweaks().end());
    registry.toggle(*darkTweak);
    const auto& theme = setting(catalog, "theme");
    CHECK(f.controller.current(theme) == option(theme, "dark"));

    const auto& widgets = setting(catalog, "widgets");
    f.controller.select(widgets, option(widgets, "off"));
    const auto widgetTweak = std::ranges::find(registry.catalog().tweaks(), std::string("widgets"), &Tweak::id);
    REQUIRE(widgetTweak != registry.catalog().tweaks().end());
    CHECK(registry.checked(*widgetTweak));
    CHECK(f.state.changes().size() == 4); // two theme values + the widget policies of either Windows: no duplicates
}

TEST_CASE("form: an empty Start is a policy value (Windows 11) and a layout file (Windows 10)") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& pins = setting(catalog, "start-pins");
    CHECK(f.controller.current(pins) == option(pins, "on"));
    f.controller.select(pins, option(pins, "off"));
    REQUIRE(f.state.changes().size() == 2);

    // The value the Applier will write is the JSON Windows expects, quotes and all.
    const auto& policy = f.state.changes().operations()[0];
    CHECK(policy.kind == OpKind::SetRegistryValue);
    const auto written = core::registryWriteFrom(policy.target, policy.value);
    REQUIRE(written);
    CHECK(written->name == L"ConfigureStartPins");
    CHECK(written->type == REG_SZ);
    CHECK(std::wstring(reinterpret_cast<const wchar_t*>(written->data.data()), written->data.size() / 2 - 1) ==
          LR"({"pinnedList":[]})");

    const auto* file = f.state.changes().find(
        OpKind::WriteFile, L"Users\\Default\\AppData\\Local\\Microsoft\\Windows\\Shell\\LayoutModification.xml");
    REQUIRE(file);
    CHECK(file->value.starts_with(L"<LayoutModificationTemplate "));
    CHECK(file->value.find(L"<defaultlayout:StartLayout GroupCellWidth=\"6\" />") != std::wstring::npos);
    CHECK(file->value.find(L"start:Tile") == std::wstring::npos); // no tile: that is the point
    CHECK(f.controller.current(pins) == option(pins, "off"));
    CHECK(f.controller.changedCount() == 1);

    // A preset keeps the file: the queue survives the trip through its JSON.
    const auto back = core::ops::ChangeSet::fromJson(f.state.changes().toJson());
    REQUIRE(back);
    CHECK(ImageSettingsController::optionIn(*back, pins) == option(pins, "off"));

    f.controller.select(pins, pins.defaultOption);
    CHECK(f.state.changes().empty());

    // The promoted apps: the policy alone only counts on Enterprise, the default profile's values do the work.
    const auto& promoted = setting(catalog, "consumer-features");
    f.controller.select(promoted, option(promoted, "off"));
    CHECK(f.state.changes().count(OpKind::SetRegistryFirstLogon) == 7);
    CHECK(f.state.changes().find(
        OpKind::SetRegistryFirstLogon,
        L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager::SilentInstalledAppsEnabled"));
}

TEST_CASE("form: the taskbar comes with File Explorer only — a layout file and the value that points Windows at it") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& pins = setting(catalog, "taskbar-pins");
    f.controller.select(pins, option(pins, "off"));
    const auto* file = f.state.changes().find(OpKind::WriteFile, L"ProgramData\\WinLove\\TaskbarLayoutModification.xml");
    REQUIRE(file);
    CHECK(file->value.find(L"PinListPlacement=\"Replace\"") != std::wstring::npos);
    CHECK(file->value.find(L"Microsoft.Windows.Explorer") != std::wstring::npos);
    // REG_EXPAND_SZ, as Microsoft documents the value: Explorer expands %ProgramData% itself.
    const auto* value = f.state.changes().find(
        OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer::LayoutXMLPath");
    REQUIRE(value);
    const auto written = core::registryWriteFrom(value->target, value->value);
    REQUIRE(written);
    CHECK(written->type == REG_EXPAND_SZ);
    CHECK(std::wstring(reinterpret_cast<const wchar_t*>(written->data.data()), written->data.size() / 2 - 1) ==
          L"%ProgramData%\\WinLove\\TaskbarLayoutModification.xml");
    // The Start layout is another file: both settings can be off together.
    const auto& start = setting(catalog, "start-pins");
    f.controller.select(start, option(start, "off"));
    CHECK(f.state.changes().count(OpKind::WriteFile) == 2);
    CHECK(f.controller.current(pins) == option(pins, "off"));
    CHECK(f.controller.current(start) == option(start, "off"));
}

TEST_CASE("form: Windows Update options share the AU key without stepping on each other") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& automatic = setting(catalog, "wu-auto");
    const auto& restart = setting(catalog, "wu-restart");
    const auto& defer = setting(catalog, "wu-feature-defer");
    f.controller.select(automatic, option(automatic, "notify"));
    f.controller.select(restart, option(restart, "off"));
    f.controller.select(defer, option(defer, "365"));
    CHECK(f.controller.current(automatic) == option(automatic, "notify"));
    CHECK(f.controller.current(restart) == option(restart, "off"));
    CHECK(f.controller.current(defer) == option(defer, "365"));
    // "Off" replaces what "notify" wrote, the restart value stays.
    f.controller.select(automatic, option(automatic, "off"));
    CHECK(f.controller.current(automatic) == option(automatic, "off"));
    CHECK(f.controller.current(restart) == option(restart, "off"));
    CHECK_FALSE(f.state.changes().find(
        OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU::AUOptions"));
    f.controller.select(defer, option(defer, "180"));
    const auto* days = f.state.changes().find(
        OpKind::SetRegistryValue,
        L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate::DeferFeatureUpdatesPeriodInDays");
    REQUIRE(days);
    CHECK(days->value == L"dword:000000b4");

    // Desktop icons are "off" by default: switching one on is the change.
    const auto& thisPc = setting(catalog, "desktop-this-pc");
    CHECK(f.controller.current(thisPc) == option(thisPc, "off"));
    f.controller.select(thisPc, option(thisPc, "on"));
    CHECK(f.controller.current(thisPc) == option(thisPc, "on"));
}

TEST_CASE("settings catalog: a file outside the default profile and ProgramData is a malformed setting") {
    const auto catalog = ImageSettingsCatalog::parse(R"({
      "format": "winlove.catalog.settings",
      "tabs": [ { "id": "t", "tr": "T", "en": "T" } ],
      "sections": [ { "id": "s", "tab": "t", "tr": "S", "en": "S" } ],
      "settings": [
        { "id": "ok", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "on",
          "files": [ { "path": "ProgramData\\WinLove\\note.txt", "content": "x" } ] },
        { "id": "system32", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "on",
          "files": [ { "path": "Windows\\System32\\x.dll", "content": "x" } ] },
        { "id": "up", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "on",
          "files": [ { "path": "Users\\Default\\..\\Public\\x.txt", "content": "x" } ] }
      ] })");
    REQUIRE(catalog);
    REQUIRE(catalog->settings().size() == 1);
    CHECK(catalog->settings().front().id == "ok");
    CHECK(catalog->settings().front().options[0].files.size() == 1);
}

TEST_CASE("form: 'apply recommended' picks every recommended option once") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto expected = std::ranges::count_if(catalog.settings(), [](const ImageSetting& s) { return s.recommended >= 0; });
    REQUIRE(expected > 0);
    // One already chosen by hand, another set against the recommendation.
    const auto& ext = setting(catalog, "file-ext");
    f.controller.select(ext, ext.recommended);
    const auto& telemetry = setting(catalog, "telemetry");
    f.controller.select(telemetry, option(telemetry, "required"));

    CHECK(f.controller.applyRecommended() == expected - 1);
    for (const auto& s : catalog.settings()) {
        if (s.recommended >= 0) {
            CAPTURE(s.id);
            CHECK(f.controller.current(s) == s.recommended);
        }
    }
    CHECK(f.controller.changedCount() == expected);
    CHECK(f.controller.applyRecommended() == 0);
}
