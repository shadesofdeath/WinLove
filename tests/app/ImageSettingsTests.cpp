// P12: the settings catalog (shipped file, malformed entries) and the form ↔ queue mapping.
#include "app/controllers/ImageSettingsController.h"
#include "base/Utf8.h"

#include <doctest.h>
#include <json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
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
    CHECK(catalog.tabs().size() == 10);
    // Every tab has something in it.
    for (const auto& tab : catalog.tabs()) {
        CAPTURE(tab.id);
        CHECK(std::ranges::any_of(catalog.settings(), [&](const ImageSetting& s) {
            const auto section = std::ranges::find(catalog.sections(), s.section, &ImageSettingSection::id);
            return section != catalog.sections().end() && section->tab == tab.id;
        }));
    }

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
        if (s.control == ImageSetting::Control::Toggle || ImageSettingsController::takesValue(s)) {
            CHECK(s.options.size() == 2);
        } else {
            for (const auto& o : s.options) {
                CHECK_FALSE(o.label.tr.empty());
                CHECK_FALSE(o.label.en.empty());
            }
        }
        // Every value lands in a hive an offline image has (the Applier would refuse it otherwise).
        for (const auto& o : s.options) {
            for (const auto& w : o.writes) {
                CAPTURE(w.key);
                CHECK(core::mapOfflineKey(w.key).has_value());
            }
        }
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

TEST_CASE("settings catalog: two settings write the same value only where meant to") {
    // Otherwise choosing one silently takes the other's value out of the queue. Shared on purpose:
    // a dropdown and a text box for one value, and Spotlight with its lock-screen tips.
    const std::set<std::wstring> allowed{
        LR"(HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management::PagingFiles)",
        LR"(HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\Explorer::SettingsPageVisibility)",
        LR"(HKLM\SOFTWARE\Policies\Microsoft\Windows NT\DNSClient::NameServer)",
        LR"(HKCU\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager::RotatingLockScreenOverlayEnabled)",
    };
    const auto catalog = shippedCatalog();
    std::map<std::wstring, std::set<std::string>> owners;
    for (const auto& s : catalog.settings()) {
        for (const auto& o : s.options) {
            for (const auto& w : o.writes) {
                owners[core::registryTarget(w)].insert(s.id);
            }
            for (const auto& [service, start] : o.services) {
                owners[L"service:" + service].insert(s.id);
            }
        }
    }
    for (const auto& [slot, ids] : owners) {
        CAPTURE(slot);
        CHECK((ids.size() == 1 || allowed.contains(slot)));
    }
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

TEST_CASE("form: promoted apps are a policy and the default profile's values (first logon)") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    // The promoted apps: the policy alone only counts on Enterprise, the default profile's values do the work.
    const auto& promoted = setting(catalog, "consumer-features");
    f.controller.select(promoted, option(promoted, "off"));
    CHECK(f.state.changes().count(OpKind::SetRegistryFirstLogon) == 7);
    CHECK(f.state.changes().find(
        OpKind::SetRegistryFirstLogon,
        L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager::SilentInstalledAppsEnabled"));
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

TEST_CASE("form: OEM information is typed text, written as strings; clearing it is the Windows default") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& maker = setting(catalog, "oem-manufacturer");
    REQUIRE(maker.control == ImageSetting::Control::Text);
    CHECK(f.controller.value(maker).empty());
    CHECK(f.controller.current(maker) == maker.defaultOption);

    CHECK(f.controller.setValue(maker, L"Berkay Bilgisayar \"Özel\""));
    CHECK(f.controller.value(maker) == L"Berkay Bilgisayar \"Özel\"");
    CHECK(f.controller.current(maker) == 1);
    REQUIRE(f.state.changes().size() == 1);
    const auto& op = f.state.changes().operations().front();
    CHECK(op.target == L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OEMInformation::Manufacturer");
    const auto written = core::registryWriteFrom(op.target, op.value);
    REQUIRE(written);
    CHECK(written->type == REG_SZ);

    // Typing on replaces the value, it does not add another.
    CHECK(f.controller.setValue(maker, L"Berkay"));
    CHECK(f.state.changes().size() == 1);
    CHECK(f.controller.value(maker) == L"Berkay");
    // Control characters are dropped, the length is capped.
    CHECK(f.controller.setValue(maker, L"a\tb\r\n" + std::wstring(500, L'x')));
    CHECK(f.controller.value(maker).starts_with(L"ab"));
    CHECK(f.controller.value(maker).size() == ImageSettingsController::kTextLimit);
    // A preset keeps the text.
    const auto back = core::ops::ChangeSet::fromJson(f.state.changes().toJson());
    REQUIRE(back);
    CHECK(ImageSettingsController::valueIn(*back, maker) == f.controller.value(maker));

    CHECK(f.controller.setValue(maker, L""));
    CHECK(f.state.changes().empty());
    CHECK(f.controller.changedCount() == 0);
}

TEST_CASE("form: a wallpaper is a JPEG of this PC copied into the image, with the values that point at it") {
    // The shipped catalog has no file setting since D-056 (Kişiselleştirme replaces Windows' own
    // pictures); the File control stays, so it is tested with an entry of its own.
    auto doc = nlohmann::json::parse(shippedJson(L"settings.json"));
    doc["settings"].push_back(nlohmann::json::parse(R"({ "id": "wallpaper", "section": "desktop", "control": "file",
        "tr": "Duvar kağıdı", "en": "Wallpaper", "copy": "ProgramData\\WinLove\\wallpaper.jpg",
        "writes": [ { "key": "HKCU\\Control Panel\\Desktop", "name": "Wallpaper", "value": "\"C:\\\\ProgramData\\\\WinLove\\\\wallpaper.jpg\"" },
                    { "key": "HKCU\\Control Panel\\Desktop", "name": "WallpaperStyle", "value": "\"10\"" },
                    { "key": "HKCU\\Control Panel\\Desktop", "name": "TileWallpaper", "value": "\"0\"" } ] })"));
    auto parsed = ImageSettingsCatalog::parse(doc.dump());
    REQUIRE(parsed);
    struct {
        AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    } holder;
    holder.state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    struct Local {
        AppState& state;
        ImageSettingsController controller;
    } f{holder.state, ImageSettingsController{holder.state, std::move(*parsed)}};
    const auto& catalog = f.controller.catalog();
    const auto& wallpaper = setting(catalog, "wallpaper");
    REQUIRE(wallpaper.control == ImageSetting::Control::File);

    const auto picture = scratch(L"duvar kağıdı.jpg");
    {
        std::ofstream out(picture, std::ios::binary);
        out << std::string(2048, 'j');
    }
    // Not a JPEG, or not there: refused, nothing queued.
    CHECK_FALSE(f.controller.setValue(wallpaper, scratch(L"missing.jpg").wstring()));
    CHECK_FALSE(f.controller.setValue(wallpaper, scratch(L"settings.json").wstring()));
    CHECK(f.state.changes().empty());

    CHECK(f.controller.setValue(wallpaper, picture.wstring()));
    CHECK(f.controller.value(wallpaper) == picture.wstring());
    CHECK(f.controller.current(wallpaper) == 1);
    const auto* copy = f.state.changes().find(OpKind::CopyFile, L"ProgramData\\WinLove\\wallpaper.jpg");
    REQUIRE(copy);
    CHECK(copy->value == picture.wstring());
    CHECK(copy->sizeDelta == 2048);
    CHECK(f.state.changes().find(OpKind::SetRegistryValue, L"HKCU\\Control Panel\\Desktop::Wallpaper"));
    CHECK(f.state.changes().size() == 4);

    // Another picture replaces the first; clearing takes everything out.
    const auto other = scratch(L"other.JPEG");
    {
        std::ofstream out(other, std::ios::binary);
        out << "jpeg";
    }
    CHECK(f.controller.setValue(wallpaper, other.wstring()));
    CHECK(f.state.changes().size() == 4);
    CHECK(f.controller.value(wallpaper) == other.wstring());
    // A bad path typed over a good one: the old picture does not stay behind in the queue.
    CHECK_FALSE(f.controller.setValue(wallpaper, L"C:\\nope"));
    CHECK(f.state.changes().empty());
}

TEST_CASE("form: right-click menu entries are verbs and handler keys in the image's classes") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& owner = setting(catalog, "context-take-ownership");
    f.controller.select(owner, option(owner, "tr"));
    // Files and folders: label, shield, command — elevated by the "runas" verb, no language-bound names.
    const auto* command = f.state.changes().find(OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Classes\\Directory\\shell\\runas\\command::");
    REQUIRE(command);
    const auto written = core::registryWriteFrom(command->target, command->value);
    REQUIRE(written);
    const std::wstring text(reinterpret_cast<const wchar_t*>(written->data.data()), written->data.size() / 2 - 1);
    CHECK(text.find(L"takeown /f \"%1\" /r") != std::wstring::npos);
    CHECK(text.find(L"*S-1-5-32-544:F") != std::wstring::npos); // Administrators by SID: "Yöneticiler" on a Turkish Windows
    CHECK(f.state.changes().size() == 10);
    f.controller.select(owner, option(owner, "en"));
    CHECK(f.state.changes().size() == 10); // the English label replaces the Turkish one
    CHECK(f.controller.current(owner) == option(owner, "en"));

    // Copy / Move To: two empty handler keys, created.
    const auto& copyMove = setting(catalog, "context-copy-move");
    f.controller.select(copyMove, option(copyMove, "on"));
    CHECK(f.controller.current(copyMove) == option(copyMove, "on"));
    // "Share" off: the handler key goes.
    const auto& share = setting(catalog, "context-share");
    f.controller.select(share, option(share, "off"));
    CHECK(f.controller.current(share) == option(share, "off"));
    const auto back = core::ops::ChangeSet::fromJson(f.state.changes().toJson());
    REQUIRE(back);
    CHECK(ImageSettingsController::optionIn(*back, copyMove) == option(copyMove, "on"));
    CHECK(ImageSettingsController::optionIn(*back, share) == option(share, "off"));
}

TEST_CASE("form: the classic Photo Viewer is the TIFF handler of the image, cloned for the other formats") {
    Fixture f;
    const auto& viewer = setting(f.controller.catalog(), "photo-viewer");
    f.controller.select(viewer, option(viewer, "on"));
    const auto* command = f.state.changes().find(
        OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Classes\\PhotoViewer.FileAssoc.Jpeg\\shell\\open\\command::");
    REQUIRE(command);
    const auto written = core::registryWriteFrom(command->target, command->value);
    REQUIRE(written);
    CHECK(written->type == REG_EXPAND_SZ); // as Windows' own TIFF entry: %ProgramFiles% is expanded when it runs
    const std::wstring text(reinterpret_cast<const wchar_t*>(written->data.data()), written->data.size() / 2 - 1);
    CHECK(text == L"%SystemRoot%\\System32\\rundll32.exe \"%ProgramFiles%\\Windows Photo Viewer\\PhotoViewer.dll\", ImageView_Fullscreen %1");
    const auto* png = f.state.changes().find(
        OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Microsoft\\Windows Photo Viewer\\Capabilities\\FileAssociations::.png");
    REQUIRE(png);
    CHECK(png->value == L"\"PhotoViewer.FileAssoc.Png\"");
    CHECK(f.controller.current(viewer) == option(viewer, "on"));
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

namespace {

const ImageSettingTemplate& preset(const ImageSettingsCatalog& catalog, std::string_view id) {
    const auto* found = catalog.findTemplate(id);
    REQUIRE(found != nullptr);
    return *found;
}

nlohmann::json readJson(const std::filesystem::path& path) {
    return nlohmann::json::parse(readFile(path));
}

} // namespace

TEST_CASE("templates (D-146): every shipped template is whole: known settings and options, both languages") {
    const auto catalog = shippedCatalog();
    const auto raw = nlohmann::json::parse(shippedJson(L"settings.json"));
    REQUIRE(raw.contains("templates"));
    REQUIRE(catalog.templates().size() == raw["templates"].size()); // none skipped
    for (const char* id : {"balanced", "privacy", "clean", "performance", "gaming", "work"}) {
        CAPTURE(id);
        CHECK(catalog.findTemplate(id) != nullptr);
    }
    CHECK(catalog.templates().front().id == "balanced"); // the safe one comes first in Şablon ▾
    std::set<std::string> ids;
    for (std::size_t t = 0; t < catalog.templates().size(); ++t) {
        const auto& preset = catalog.templates()[t];
        CAPTURE(preset.id);
        CHECK(ids.insert(preset.id).second);
        CHECK_FALSE(preset.title.tr.empty());
        CHECK_FALSE(preset.title.en.empty());
        CHECK_FALSE(preset.description.tr.empty());
        CHECK_FALSE(preset.description.en.empty());
        // Every pick of the file is in the template: no unknown setting, option or default slipped by.
        CHECK(preset.picks.size() == raw["templates"][t]["settings"].size());
        std::set<std::size_t> settings;
        for (const auto& pick : preset.picks) {
            REQUIRE(pick.setting < catalog.settings().size());
            const auto& s = catalog.settings()[pick.setting];
            CAPTURE(s.id);
            CHECK(settings.insert(pick.setting).second);
            CHECK(pick.option != s.defaultOption);
            CHECK_FALSE(ImageSettingsController::takesValue(s));
            CHECK(ImageSettingsController::templateOption(catalog, preset, s) == pick.option);
        }
        CHECK(std::ranges::is_sorted(preset.picks, {}, &ImageSettingTemplate::Pick::setting));
    }
}

TEST_CASE("templates (D-146): malformed picks and templates are skipped, the rest loads") {
    const auto catalog = ImageSettingsCatalog::parse(R"({
      "format": "winlove.catalog.settings",
      "tabs": [ { "id": "t", "tr": "T", "en": "T" } ],
      "sections": [ { "id": "s", "tab": "t", "tr": "S", "en": "S" } ],
      "settings": [
        { "id": "a", "section": "s", "control": "toggle", "tr": "a", "en": "a", "default": "on",
          "writes": [ { "key": "HKCU\\Software\\X", "name": "a", "value": "dword:00000000" } ] },
        { "id": "b", "section": "s", "control": "dropdown", "tr": "b", "en": "b",
          "options": [ { "id": "default", "tr": "d", "en": "d" },
                       { "id": "x", "tr": "x", "en": "x", "writes": [ { "key": "HKCU\\Software\\X", "name": "b", "value": "dword:00000001" } ] } ] },
        { "id": "c", "section": "s", "control": "text", "tr": "c", "en": "c", "writes": [ { "key": "HKCU\\Software\\X", "name": "c" } ] }
      ],
      "templates": [
        { "id": "one", "tr": "Bir", "en": "One", "desc_tr": "d", "desc_en": "d",
          "settings": { "b": "x", "a": "off", "missing": "off", "c": "value" } },
        { "id": "defaults", "tr": "V", "en": "V", "settings": { "a": "on", "b": "default", "b2": "x" } },
        { "id": "one", "tr": "Yine", "en": "Again", "settings": { "a": "off" } },
        { "id": "bad-option", "tr": "K", "en": "K", "settings": { "b": "nope" } }
      ] })");
    REQUIRE(catalog);
    REQUIRE(catalog->templates().size() == 1);
    const auto& one = catalog->templates().front();
    CHECK(one.title.tr == L"Bir");
    REQUIRE(one.picks.size() == 2); // "missing" and the text setting "c" are dropped
    CHECK(one.picks[0].setting == 0); // in the catalog's order, not the file's
    CHECK(one.picks[0].option == 0);  // toggle "a" off (default on)
    CHECK(one.picks[1].setting == 1);
    CHECK(one.picks[1].option == 1);
    CHECK(catalog->findTemplate("defaults") == nullptr); // only the default / unknown: nothing left
    CHECK(catalog->findTemplate("bad-option") == nullptr);
}

TEST_CASE("templates (D-146): two templates never give one setting different options") {
    const auto catalog = shippedCatalog();
    std::map<std::size_t, std::pair<int, std::string>> given;
    for (const auto& preset : catalog.templates()) {
        for (const auto& pick : preset.picks) {
            CAPTURE(preset.id);
            CAPTURE(catalog.settings()[pick.setting].id);
            const auto [it, fresh] = given.try_emplace(pick.setting, pick.option, preset.id);
            CHECK((fresh || it->second.first == pick.option));
        }
    }
}

TEST_CASE("templates (D-146): none takes what breaks Windows; Dengeli only low-risk, reversible values") {
    const auto catalog = shippedCatalog();
    // Never in a template: security off, Windows Update / Store updates off, networking, printing,
    // accessibility, sign-in, device access, page file, or what cannot be taken back here.
    const std::set<std::string> never{
        "defender", "smartscreen", "uac-level", "uac-dim", "motw", "smart-app-control", "memory-integrity", "vbs",
        "lsa-protection", "device-encryption", "remote-assistance", "workplace-join", "wpbt",
        "wu-auto", "wu-drivers", "wu-msrt", "store-auto-update", "wu-restart", "wu-feature-defer", "wu-delivery",
        "print-spooler", "web-printing", "vendor-apps", "ipv6", "dns-servers", "doh", "smb-signing", "smb-guest",
        "wifi-hotspots", "new-network-prompt", "accessibility-shortcuts", "caps-lock", "notifications",
        "perm-camera", "perm-microphone", "location", "find-my-device", "pagefile-mode", "xbox-services",
        "search-indexing", "sysmain", "background-apps", "sensor-services", "compat-engine", "ntfs-8dot3",
        "auto-end-tasks", "hibernation", "power-throttling", "mpo", "start-all-apps", "removable-twice",
        "ctx-new-bmp-rtf", "context-share", "lockscreen-off", "ps-execution-policy", "developer-mode", "fast-startup"};
    for (const auto& preset : catalog.templates()) {
        for (const auto& pick : preset.picks) {
            const auto& s = catalog.settings()[pick.setting];
            CAPTURE(preset.id);
            CAPTURE(s.id);
            CHECK(s.risk != core::ops::Risk::High);
            CHECK_FALSE(never.contains(s.id));
            for (const auto& w : s.options[static_cast<std::size_t>(pick.option)].writes) {
                CHECK(w.kind == core::RegistryWrite::Kind::Set); // a deletion has no way back
            }
        }
    }
    // Dengeli is for anyone: low risk only, registry values only (no service start, no file).
    const auto& balanced = preset(catalog, "balanced");
    CHECK(balanced.picks.size() >= 15);
    for (const auto& pick : balanced.picks) {
        const auto& s = catalog.settings()[pick.setting];
        const auto& o = s.options[static_cast<std::size_t>(pick.option)];
        CAPTURE(s.id);
        CHECK(s.risk == core::ops::Risk::Low);
        CHECK(o.services.empty());
        CHECK(o.files.empty());
        CHECK(o.copyTo.empty());
        CHECK_FALSE(o.writes.empty());
    }
}

TEST_CASE("templates (D-146): applying queues exactly the template; Sıfırla and undo take it back") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& privacy = preset(catalog, "privacy");
    const auto result = f.controller.applyTemplate(privacy);
    CHECK(result.changed == static_cast<int>(privacy.picks.size()));
    CHECK(result.already == 0);
    CHECK(result.kept == 0);
    CHECK(std::ranges::any_of(result.risky, [](const ImageSetting* s) { return s->id == "diagtrack-service"; }));
    for (const ImageSetting* risky : result.risky) {
        CHECK(risky->risk != core::ops::Risk::Low);
    }
    for (const auto& s : catalog.settings()) {
        CAPTURE(s.id);
        const int given = ImageSettingsController::templateOption(catalog, privacy, s);
        CHECK(f.controller.current(s) == (given >= 0 ? given : s.defaultOption));
    }
    CHECK(f.controller.changedCount() == static_cast<int>(privacy.picks.size()));
    // Every queued operation is one of the template's options.
    std::size_t expected = 0;
    for (const auto& pick : privacy.picks) {
        expected += ImageSettingsController::operationsFor(catalog.settings()[pick.setting], pick.option).size();
    }
    CHECK(f.state.changes().size() == expected);
    // Again: nothing to do.
    const auto again = f.controller.applyTemplate(privacy);
    CHECK(again.changed == 0);
    CHECK(again.already == static_cast<int>(privacy.picks.size()));

    CHECK(f.controller.resetChanges() == static_cast<int>(privacy.picks.size()));
    CHECK(f.state.changes().empty());
    CHECK(f.controller.changedCount() == 0);
    CHECK(f.controller.resetChanges() == 0);

    // One template is one undo step.
    f.controller.applyTemplate(preset(catalog, "performance"));
    CHECK_FALSE(f.state.changes().empty());
    CHECK(f.state.undoQueue());
    CHECK(f.state.changes().empty());
}

TEST_CASE("templates (D-146): what the user chose stays; templates stack in any order") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& balanced = preset(catalog, "balanced");
    const auto& telemetry = setting(catalog, "telemetry");
    const auto& ext = setting(catalog, "file-ext");
    REQUIRE(ImageSettingsController::templateOption(catalog, balanced, telemetry) == option(telemetry, "security"));
    f.controller.select(telemetry, option(telemetry, "required")); // the user's own, not the template's
    f.controller.select(ext, option(ext, "on"));                   // the template's already
    const auto preview = f.controller.previewTemplate(balanced);
    const auto result = f.controller.applyTemplate(balanced);
    CHECK(preview.changed == result.changed);
    CHECK(result.kept == 1);
    CHECK(result.already == 1);
    CHECK(result.changed == static_cast<int>(balanced.picks.size()) - 2);
    CHECK(f.controller.current(telemetry) == option(telemetry, "required"));
    CHECK(f.controller.previewTemplate(balanced).changed == 0);

    // A setting the user took back to the Windows default is a choice too (the image has another).
    // Here: stacking. Gizlilik after Dengeli, and Dengeli after Gizlilik, end the same.
    Fixture a;
    Fixture b;
    const auto& privacy = preset(catalog, "privacy");
    a.controller.applyTemplate(preset(a.controller.catalog(), "balanced"));
    a.controller.applyTemplate(preset(a.controller.catalog(), "privacy"));
    b.controller.applyTemplate(preset(b.controller.catalog(), "privacy"));
    b.controller.applyTemplate(preset(b.controller.catalog(), "balanced"));
    for (const auto& s : a.controller.catalog().settings()) {
        CAPTURE(s.id);
        CHECK(a.controller.current(s) == b.controller.current(s));
        const int fromPrivacy = ImageSettingsController::templateOption(a.controller.catalog(), *a.controller.catalog().findTemplate("privacy"), s);
        const int fromBalanced = ImageSettingsController::templateOption(a.controller.catalog(), *a.controller.catalog().findTemplate("balanced"), s);
        const int expected = fromPrivacy >= 0 ? fromPrivacy : fromBalanced >= 0 ? fromBalanced : s.defaultOption;
        CHECK(a.controller.current(s) == expected);
    }
    CHECK(privacy.picks.size() > balanced.picks.size());
}

TEST_CASE("templates (D-146): the image's own value is not a choice; a template changes it") {
    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& balanced = preset(catalog, "balanced");
    const auto& telemetry = setting(catalog, "telemetry");
    // The image already has "Gerekli" (D-045); the template's "Güvenlik" replaces it.
    AppState::ImageValues values{AppState::ImageValues::Status::Ready, f.state.mounted()->mountDir, {}, {}, {}};
    for (const auto& op : ImageSettingsController::operationsFor(telemetry, option(telemetry, "required"))) {
        values.held.insert(AppState::imageValueKey(op.kind, op.target, op.value));
    }
    f.state.setImageValues(std::move(values));
    REQUIRE(f.controller.imageOption(telemetry) == option(telemetry, "required"));
    const auto result = f.controller.applyTemplate(balanced);
    CHECK(result.kept == 0);
    CHECK(f.controller.current(telemetry) == option(telemetry, "security"));
    f.controller.resetChanges();
    CHECK(f.controller.current(telemetry) == option(telemetry, "required")); // back to the image's
}

TEST_CASE("templates (D-146): the page's texts exist in Turkish and English") {
    const auto dir = std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings";
    for (const wchar_t* file : {L"tr.json", L"en.json"}) {
        const auto strings = readJson(dir / file);
        const std::string name = utf8::fromWide(file);
        CAPTURE(name);
        REQUIRE(strings.contains("tweaks"));
        for (const char* key : {"templateAction", "templateApplied", "templateNone", "templateKept", "templateRisky",
                                "templateApply", "templatePreview", "templateOption", "filterAll", "filterTemplate",
                                "undo", "redo", "resetAction"}) {
            CAPTURE(key);
            REQUIRE(strings["tweaks"].contains(key));
            CHECK_FALSE(strings["tweaks"][key].get<std::string>().empty());
        }
        CHECK_FALSE(strings["tweaks"].contains("applyRecommended")); // Şablon ▾ took its place
    }
}

TEST_CASE("form (D-062): page file typed as a drive and sizes, written as REG_MULTI_SZ; the new desktop / Control Panel / Defender rows") {
    CHECK(pagefileEntry(L"d: 4096 8192") == L"D:\\pagefile.sys 4096 8192");
    CHECK(pagefileEntry(L"  E  ") == L"E:\\pagefile.sys 0 0");
    CHECK_FALSE(pagefileEntry(L"D: 4096"));          // one size
    CHECK_FALSE(pagefileEntry(L"D: 8192 4096"));     // largest first
    CHECK_FALSE(pagefileEntry(L"D: 8 4096"));        // under 16 MB
    CHECK_FALSE(pagefileEntry(L"DD: 4096 8192"));
    CHECK_FALSE(pagefileEntry(L"D: 40x6 8192"));
    CHECK(pagefileTyped(L"D:\\pagefile.sys 4096 8192") == L"D: 4096 8192");
    CHECK(pagefileTyped(L"C:\\pagefile.sys 0 0") == L"C:");
    CHECK(pagefileTyped(L"?:\\pagefile.sys").empty() == false); // the automatic entry: "?:" (shown as it is)

    Fixture f;
    const auto& catalog = f.controller.catalog();
    const auto& custom = setting(catalog, "pagefile-custom");
    REQUIRE(custom.format == ImageSetting::Format::Pagefile);
    CHECK_FALSE(f.controller.setValue(custom, L"D: 40"));  // half typed: nothing queued, the row says so
    CHECK(f.state.changes().empty());
    CHECK(f.controller.setValue(custom, L"d: 2048 4096"));
    CHECK(f.controller.value(custom) == L"D: 2048 4096");
    REQUIRE(f.state.changes().size() == 1);
    const auto& op = f.state.changes().operations().front();
    CHECK(op.target == L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management::PagingFiles");
    const auto written = core::registryWriteFrom(op.target, op.value);
    REQUIRE(written);
    CHECK(written->type == REG_MULTI_SZ);
    const std::wstring expected = std::wstring(L"D:\\pagefile.sys 2048 4096") + L'\0' + L'\0';
    REQUIRE(written->data.size() == expected.size() * sizeof(wchar_t));
    CHECK(std::memcmp(written->data.data(), expected.data(), written->data.size()) == 0);
    const auto back = core::ops::ChangeSet::fromJson(f.state.changes().toJson());
    REQUIRE(back);
    CHECK(ImageSettingsController::valueIn(*back, custom) == L"D: 2048 4096");

    // The dropdown and the typed value share the slot: the one set last wins.
    const auto& mode = setting(catalog, "pagefile-mode");
    f.controller.select(mode, option(mode, "off"));
    CHECK(f.state.changes().size() == 1);
    const auto off = core::registryWriteFrom(f.state.changes().operations().front().target, f.state.changes().operations().front().value);
    REQUIRE(off);
    CHECK(off->type == REG_MULTI_SZ);
    CHECK(off->data.size() == 2 * sizeof(wchar_t)); // empty list: no page file

    const auto& icons = setting(catalog, "desktop-icon-size");
    CHECK(icons.firstLogon);
    f.controller.select(icons, option(icons, "small"));
    CHECK(f.state.changes().find(OpKind::SetRegistryFirstLogon, L"HKCU\\Software\\Microsoft\\Windows\\Shell\\Bags\\1\\Desktop::IconSize"));
    const auto& view = setting(catalog, "control-panel-view");
    f.controller.select(view, option(view, "small"));
    CHECK(f.state.changes().find(OpKind::SetRegistryValue,
                                 L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ControlPanel::AllItemsIconView"));
    const auto& defender = setting(catalog, "defender");
    f.controller.select(defender, 0); // off
    CHECK(f.state.changes().find(OpKind::SetServiceStart, L"WinDefend")->value == L"disabled");
    CHECK(f.state.changes().find(OpKind::SetServiceStart, L"WdFilter"));
}
