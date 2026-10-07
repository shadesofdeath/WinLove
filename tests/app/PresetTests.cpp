// P15: preset files, the library folder, a preset's changes as named items, the A / B diff.
#include "app/controllers/ImageSettingsController.h"
#include "app/controllers/PostSetupController.h"
#include "app/controllers/PresetController.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace wl;
using namespace wl::app;
using core::ops::OpKind;
using core::ops::Operation;
using Mark = PresetController::DiffRow::Mark;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"presets";
    std::filesystem::create_directories(dir);
    return dir / name;
}

struct Fixture {
    Localization strings = *Localization::fromJson(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/strings/en.json"));
    ImageSettingsCatalog catalog =
        *ImageSettingsCatalog::parse(readFile(std::filesystem::path(WL_SOURCE_DIR) / L"resources/catalog/settings.json"));
    AppState state{scratch(L"recent.json"), scratch(L"settings.json")};
    std::filesystem::path folder = scratch(L"library");
    PresetController controller{state, catalog, strings, Language::English, folder};

    Fixture() {
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        state.setMounted(MountedImage{L"C:\\m", L"C:\\w\\install.wim", 1, L"Pro"});
    }

    void choose(core::ops::ChangeSet& set, std::string_view id, std::string_view option) const {
        const auto s = std::ranges::find(catalog.settings(), id, &ImageSetting::id);
        REQUIRE(s != catalog.settings().end());
        const auto o = std::ranges::find(s->options, option, &ImageSettingOption::id);
        REQUIRE(o != s->options.end());
        set.addAll(ImageSettingsController::operationsFor(*s, static_cast<int>(o - s->options.begin())));
    }
};

Operation appx(const wchar_t* identity) {
    return Operation{OpKind::RemoveAppx, std::wstring(identity) + L"_1.0.0.0_neutral_~_8wekyb3d8bbwe"};
}

const PresetController::Item* find(const std::vector<PresetController::Item>& items, std::wstring_view label) {
    const auto it = std::ranges::find(items, label, &PresetController::Item::label);
    return it == items.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("preset file: ChangeSet + name + answer file; plain ChangeSet files and older readers still work") {
    Preset preset;
    preset.name = L"Ofis G\u00fcvenli";
    preset.changes.add(appx(L"Microsoft.BingNews"));
    preset.unattend = AppState::Unattend{};
    preset.unattend->options.accountName = L"ofis";
    preset.unattend->options.password = L"gizli";
    preset.unattend->options.bypassTpm = true;
    preset.unattend->includeInIso = true;
    preset.unattend->options.compactOs = true;                  // D-056
    preset.bootDrivers = {LR"(D:\drivers\vmd\iaStorVD.inf)"}; // D-056: not part of the queue

    const std::string json = presetToJson(preset);
    CHECK(json.find("gizli") == std::string::npos); // as in the answer file: not in clear text
    const auto back = presetFromJson(json, L"fallback");
    REQUIRE(back);
    CHECK(back->name == preset.name);
    CHECK(back->changes.size() == 1);
    REQUIRE(back->unattend);
    CHECK(back->unattend->options == preset.unattend->options);
    CHECK(back->unattend->includeInIso);
    CHECK(back->unattend->options.compactOs);
    CHECK(back->bootDrivers == preset.bootDrivers);

    // What "Preset olarak kaydet" and wlcli write: no name, no answer file.
    const auto plain = presetFromJson(preset.changes.toJson(), L"from-file-name");
    REQUIRE(plain);
    CHECK(plain->name == L"from-file-name");
    CHECK_FALSE(plain->unattend);
    // And the other way round: a ChangeSet reader takes a preset file.
    const auto asChangeSet = core::ops::ChangeSet::fromJson(json);
    REQUIRE(asChangeSet);
    CHECK(asChangeSet->size() == 1);

    CHECK_FALSE(presetFromJson("{\"format\":\"something\"}", L"x"));
    CHECK_FALSE(presetFromJson(R"({"format":"winlove.changeset","version":1,"operations":[],"unattend":{"xml":"<html/>"}})", L"x"));
}

TEST_CASE("preset library: save the current state, list by name, import, export, delete") {
    Fixture f;
    f.controller.reload();
    CHECK(f.controller.presets().empty());

    f.state.queue(appx(L"Microsoft.BingNews"));
    REQUIRE(f.controller.saveCurrent(L"Zeta: slim?"));
    CHECK(std::filesystem::exists(f.folder / L"Zeta slim.wlpreset")); // a name Windows accepts
    AppState::Unattend unattend;
    unattend.options.accountName = L"admin";
    f.state.setUnattend(unattend);
    f.state.queue(appx(L"Microsoft.ZuneMusic"));
    REQUIRE(f.controller.saveCurrent(L"alpha"));

    REQUIRE(f.controller.presets().size() == 2);
    CHECK(f.controller.presets()[0].name == L"alpha"); // sorted, case-insensitively
    CHECK(f.controller.presets()[1].name == L"Zeta: slim?");
    CHECK(f.controller.presets()[0].changes.size() == 2);
    REQUIRE(f.controller.presets()[0].unattend);
    CHECK_FALSE(f.controller.presets()[1].unattend); // nothing was answered when it was saved

    const auto exported = scratch(L"exported.wlpreset");
    REQUIRE(f.controller.exportTo(0, exported));
    REQUIRE(f.controller.remove(0));
    REQUIRE(f.controller.presets().size() == 1);
    REQUIRE(f.controller.importFile(exported));
    REQUIRE(f.controller.presets().size() == 2);
    CHECK(f.controller.presets()[0].name == L"alpha");

    const auto junk = scratch(L"junk.wlpreset");
    {
        std::ofstream out(junk, std::ios::binary);
        out << "not a preset";
    }
    CHECK_FALSE(f.controller.importFile(junk));
    CHECK(f.controller.presets().size() == 2);
    CHECK_FALSE(f.controller.remove(7));
    CHECK(PresetController::fileNameFor(L"  ...  ") == L"preset");
}

TEST_CASE("preset items: settings by name, components by identity, steps one by one, no password") {
    Fixture f;
    Preset preset;
    preset.changes.addAll({appx(L"Microsoft.XboxGamingOverlay"), Operation{OpKind::SetServiceStart, L"Fax", L"manual"},
                           Operation{OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\Contoso::Mode", L"dword:00000001"},
                           Operation{OpKind::EnableFeature, L"NetFx3", L""}});
    f.choose(preset.changes, "advertising-id", "off"); // two registry writes, first-logon kind
    f.choose(preset.changes, "telemetry", "security");
    f.choose(preset.changes, "search-indexing", "off"); // a service setting
    core::PostSetupPlan plan;
    plan.steps = {{core::PostSetupStep::Type::Winget, L"7-Zip", L"7zip.7zip", {}, true},
                  {core::PostSetupStep::Type::Command, L"", L"shutdown /r /t 30", {}, false}};
    preset.changes.add(PostSetupController::operationFor(plan, 0));
    preset.unattend = AppState::Unattend{};
    preset.unattend->options.accountName = L"admin";
    preset.unattend->options.password = L"gizli";
    preset.unattend->options.bypassTpm = true;

    const auto items = f.controller.items(preset);
    // Settings: one item each, with the option as the value — and their raw writes are not listed again.
    REQUIRE(find(items, L"Advertising ID"));
    CHECK(find(items, L"Advertising ID")->value == L"Off");
    REQUIRE(find(items, L"Telemetry level"));
    CHECK(find(items, L"Telemetry level")->value == L"Security (lowest)");
    REQUIRE(find(items, L"Search indexing service"));
    CHECK(std::ranges::none_of(items, [](const auto& i) { return i.label.find(L"AdvertisingInfo") != std::wstring::npos; }));
    CHECK_FALSE(find(items, L"WSearch"));
    // The rest as it is.
    REQUIRE(find(items, L"Microsoft.XboxGamingOverlay"));
    CHECK(find(items, L"Microsoft.XboxGamingOverlay")->value == L"remove");
    REQUIRE(find(items, L"Fax"));
    CHECK(find(items, L"Fax")->value == f.strings.get(Str::ServicesStartManual));
    REQUIRE(find(items, L"HKLM\\SOFTWARE\\Contoso::Mode"));
    REQUIRE(find(items, L"NetFx3"));
    CHECK(find(items, L"NetFx3")->value == L"enable");
    REQUIRE(find(items, L"7-Zip"));
    CHECK(find(items, L"7-Zip")->value == L"winget");
    REQUIRE(find(items, L"shutdown /r /t 30"));
    // Answer file: what was answered; the password only as "set".
    REQUIRE(find(items, f.strings.get(Str::UnattendedLocalAccount)));
    CHECK(find(items, f.strings.get(Str::UnattendedLocalAccount))->value == L"admin");
    REQUIRE(find(items, f.strings.get(Str::UnattendedPassword)));
    CHECK(find(items, f.strings.get(Str::UnattendedPassword))->value == L"set");
    CHECK(std::ranges::none_of(items, [](const auto& i) { return i.value == L"gizli"; }));
    CHECK(items.size() == 3 + 4 + 2 + 3);
}

TEST_CASE("preset diff: added, removed, changed; identical rows only on request; grouped by category") {
    Fixture f;
    Preset a;
    a.changes.addAll({appx(L"Microsoft.BingNews"), appx(L"Microsoft.XboxGamingOverlay"),
                      Operation{OpKind::SetServiceStart, L"DiagTrack", L"disabled"}});
    f.choose(a.changes, "telemetry", "security");
    a.unattend = AppState::Unattend{};
    a.unattend->options.accountName = L"gamer";

    Preset b;
    b.changes.addAll({Operation{OpKind::SetServiceStart, L"DiagTrack", L"manual"}, appx(L"Microsoft.BingNews"),
                      appx(L"Microsoft.GetHelp")});
    f.choose(b.changes, "telemetry", "required");
    f.choose(b.changes, "web-search", "off");
    b.unattend = AppState::Unattend{};
    b.unattend->options.accountName = L"ofis";

    const auto rows = f.controller.diff(a, b, /*includeSame=*/false);
    auto row = [&](std::wstring_view label) -> const PresetController::DiffRow& {
        const auto it = std::ranges::find(rows, label, &PresetController::DiffRow::label);
        REQUIRE(it != rows.end());
        return *it;
    };
    CHECK(row(L"Microsoft.XboxGamingOverlay").mark == Mark::Removed); // only in A
    CHECK(row(L"Microsoft.XboxGamingOverlay").b.empty());
    CHECK(row(L"Microsoft.GetHelp").mark == Mark::Added); // only in B
    // A says it as the setting ("Telemetry service: Off"), B as a plain service start: one changed row.
    CHECK(row(L"Telemetry service (DiagTrack)").mark == Mark::Changed);
    CHECK(row(L"Telemetry service (DiagTrack)").a == L"Off");
    CHECK(row(L"Telemetry service (DiagTrack)").b == f.strings.get(Str::ServicesStartManual));
    CHECK(std::ranges::none_of(rows, [](const auto& r) { return r.label == L"DiagTrack"; }));
    CHECK(row(L"Telemetry level").mark == Mark::Changed);
    CHECK(row(L"Telemetry level").a == L"Security (lowest)");
    CHECK(row(L"Telemetry level").b == L"Required (basic)");
    CHECK(row(L"Web search results").mark == Mark::Added);
    CHECK(row(f.strings.get(Str::UnattendedLocalAccount)).a == L"gamer");
    CHECK(row(f.strings.get(Str::UnattendedLocalAccount)).b == L"ofis");
    CHECK(std::ranges::none_of(rows, [](const auto& r) { return r.label == L"Microsoft.BingNews"; }));
    CHECK(std::ranges::is_sorted(rows, {}, &PresetController::DiffRow::category));

    const auto summary = PresetController::summarize(rows);
    CHECK(summary.added == 2);
    CHECK(summary.removed == 1);
    CHECK(summary.changed == 3);

    const auto all = f.controller.diff(a, b, /*includeSame=*/true);
    CHECK(all.size() == rows.size() + 1);
    CHECK(std::ranges::count(all, Mark::Same, &PresetController::DiffRow::mark) == 1);
    CHECK(f.controller.diff(a, a, false).empty());
}

TEST_CASE("preset diff: two settings writing one value are not told as a change against each other") {
    // "Pages hidden in Settings" (a list) and "Custom page list" (typed) both write
    // SettingsPageVisibility: one preset read twice showed "Custom page list" as changed (2026-10-07,
    // the user's own preset against the queue it had just filled).
    Fixture f;
    Preset a;
    f.choose(a.changes, "settings-home", "home-ai");
    const auto items = f.controller.items(a);
    REQUIRE(find(items, L"Pages hidden in Settings") != nullptr);
    REQUIRE(find(items, L"Custom page list") != nullptr);
    CHECK(f.controller.diff(a, a, false).empty());
    Preset b = a;
    CHECK(f.controller.diff(a, b, false).empty());
    f.choose(b.changes, "settings-home", "home"); // a real change still shows, on the list's own row
    const auto rows = f.controller.diff(a, b, false);
    const auto list = std::ranges::find(rows, L"Pages hidden in Settings", &PresetController::DiffRow::label);
    REQUIRE(list != rows.end());
    CHECK(list->mark == Mark::Changed);
    CHECK(list->a == L"Home and AI components");
    CHECK(list->b == L"Home");
}

TEST_CASE("preset apply: operations into the queue, the answer file into the state; the queue needs a mount") {
    Fixture f;
    Preset preset;
    preset.changes.addAll({appx(L"Microsoft.BingNews"), appx(L"Microsoft.GetHelp")});
    preset.unattend = AppState::Unattend{};
    preset.unattend->options.computerName = L"LAB-01";

    f.state.queue(appx(L"Microsoft.ZuneMusic")); // already queued: a preset adds to it
    CHECK(f.controller.apply(preset) == 2);
    CHECK(f.state.changes().size() == 3);
    CHECK(f.state.unattend().options.computerName == L"LAB-01");
    // The current queue as a comparable preset: nothing differs from what was just loaded + the extra app.
    const auto rows = f.controller.diff(preset, f.controller.current(), false);
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().mark == Mark::Added);
    CHECK(rows.front().label == L"Microsoft.ZuneMusic");

    f.state.setMounted(std::nullopt);
    f.state.setUnattend({});
    CHECK(f.controller.apply(preset) == 0);
    CHECK(f.state.changes().empty());
    CHECK(f.state.unattend().options.computerName == L"LAB-01"); // the answer file does not need a mount
}
