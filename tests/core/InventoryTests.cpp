// D-112: what the Bileşenler tree reads from an image (the reference tool's dynamic rows) — the pure parts:
// font families from Fonts value names, locale folder names, the INF device names per
// architecture, and the removal recipes of each kind.
#include "core/image/DriverInf.h"
#include "core/image/ImageInventory.h"

#include <doctest.h>

using namespace wl;
using namespace wl::core;

TEST_CASE("inventory: font families from the Fonts values") {
    CHECK(fontFamily(L"Arial (TrueType)") == L"Arial");
    CHECK(fontFamily(L"Arial Bold Italic (TrueType)") == L"Arial");
    CHECK(fontFamily(L"Arial Black (TrueType)") == L"Arial");
    CHECK(fontFamily(L"Cambria & Cambria Math (TrueType)") == L"Cambria");
    CHECK(fontFamily(L"Segoe UI Semilight Italic (TrueType)") == L"Segoe UI");
    CHECK(fontFamily(L"Courier 10,12,15") == L"Courier");
    CHECK(fontFamily(L"MS Sans Serif 8,10,12,14,18,24 (120)") == L"MS Sans Serif");
    CHECK(fontFamily(L"Roman (All res)") == L"Roman");
    CHECK(fontFamily(L"Gabriola (TrueType)") == L"Gabriola");
}

TEST_CASE("inventory: the fonts Windows and its web engines fall back to are held (D-119)") {
    // VM test 2026-11-08: these (with Segoe UI) were enough for Setup's screens; the rest of the preset's fonts stayed removed.
    for (const wchar_t* value : {L"Arial (TrueType)", L"Arial Bold Italic (TrueType)", L"Times New Roman Bold (TrueType)",
                                 L"Courier New Italic (TrueType)", L"Tahoma Bold (TrueType)", L"Microsoft Sans Serif (TrueType)",
                                 L"MS Sans Serif 8,10,12,14,18,24 (120)", L"Courier 10,12,15", L"Small Fonts (120)", L"Symbol (TrueType)",
                                 L"Consolas Bold (TrueType)", L"Lucida Console (TrueType)", L"Roman (All res)", L"Segoe UI Light (TrueType)",
                                 L"Segoe Fluent Icons (TrueType)", L"Webdings (TrueType)", L"Wingdings (TrueType)"}) {
        CAPTURE(std::wstring(value));
        CHECK(isCoreFontFamily(fontFamily(value)));
    }
    for (const wchar_t* value : {L"Calibri (TrueType)", L"Gabriola (TrueType)", L"Verdana (TrueType)", L"Georgia (TrueType)",
                                 L"Segoe Print (TrueType)", L"Segoe UI Historic (TrueType)", L"Comic Sans MS (TrueType)"}) {
        CAPTURE(std::wstring(value));
        CHECK_FALSE(isCoreFontFamily(fontFamily(value)));
    }
}

TEST_CASE("inventory: locale folders") {
    CHECK(isLocaleTag(L"cy-GB"));
    CHECK(isLocaleTag(L"sr-Latn-RS"));
    CHECK(isLocaleTag(L"en-US"));
    CHECK_FALSE(isLocaleTag(L"drivers"));
    CHECK_FALSE(isLocaleTag(L"0409"));
    CHECK_FALSE(isLocaleTag(L"en"));        // a language without a region: not a resource folder
    CHECK_FALSE(isLocaleTag(L"..-GB"));
}

TEST_CASE("inventory: an INF's device names, for the image's architecture only") {
    const auto inf = parseInfText(LR"([Version]
Class=Modem
ClassGuid={4D36E96D-E325-11CE-BFC1-08002BE10318}
Provider=%Mfg%
DriverVer=06/21/2006,10.0.26100.1
[Manufacturer]
%Mfg% = Models, NTamd64, NTarm64
[Models]
%Old% = Install, PCI\OLD
[Models.NTamd64]
%Dev1% = Install, PCI\A
%Dev2% = Install, PCI\B
%Dev1% = Install, PCI\C
[Models.NTarm64]
%Arm% = Install, PCI\D
[Strings]
Mfg = "Contoso"
Dev1 = "Contoso 56K Modem"
Dev2 = "Contoso Fax"
Arm = "Contoso ARM Modem"
Old = "Contoso Old"
)");
    CHECK(inf.provider == L"Contoso");
    CHECK(inf.devicesFor(L"amd64") == std::vector<std::wstring>{L"Contoso 56K Modem", L"Contoso Fax"});
    CHECK(inf.devicesFor(L"arm64") == std::vector<std::wstring>{L"Contoso ARM Modem"});
    CHECK(inf.devices.size() == 4); // every decoration, each name once
}

TEST_CASE("inventory: removal recipes follow the reference tool's methods and pass validation") {
    // Keyboard: the layout DLLs and its Keyboard Layouts key; a shared DLL stays.
    const auto keyboard = keyboardRecipe({L"000C0C00", L"Gotik", L"KBDGTHC.DLL", false});
    CHECK(keyboard.paths == std::vector<std::wstring>{L"Windows\\System32\\KBDGTHC.DLL", L"Windows\\SysWOW64\\KBDGTHC.DLL"});
    REQUIRE(keyboard.registry.size() == 1);
    CHECK(keyboard.registry[0].kind == RegistryWrite::Kind::DeleteKey);
    CHECK(keyboard.registry[0].key == L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\000C0C00");
    CHECK(validateComponentRecipe(keyboard));
    CHECK(keyboardRecipe({L"0001041F", L"Türkçe F", L"KBDTUF.DLL", true}).paths.empty());

    // Font: its files in Windows\Fonts and its Fonts values.
    const auto font = fontRecipe({L"Gabriola", {L"Gabriola (TrueType)"}, {L"Gabriola.ttf"}, 1});
    CHECK(font.paths == std::vector<std::wstring>{L"Windows\\Fonts\\Gabriola.ttf"});
    REQUIRE(font.registry.size() == 1);
    CHECK(font.registry[0].kind == RegistryWrite::Kind::DeleteValue);
    CHECK(font.registry[0].name == L"Gabriola (TrueType)");
    CHECK(validateComponentRecipe(font));

    // Language: its locale folders.
    const auto language = languageRecipe({L"cy-GB", L"Galce", {L"Windows\\System32\\cy-GB", L"Windows\\SysWOW64\\cy-GB"}, 1});
    CHECK(language.paths.size() == 2);
    CHECK(validateComponentRecipe(language));

    // System app: its folder goes, the family is deprovisioned, the inbox registration goes.
    const auto app = systemAppRecipe({L"Microsoft.Windows.PeopleExperienceHost_10.0.26100.1_neutral_neutral_cw5n1h2txyewy",
                                      L"Microsoft.Windows.PeopleExperienceHost_cw5n1h2txyewy", L"Microsoft.Windows.PeopleExperienceHost", L"",
                                      L"Windows\\SystemApps\\Microsoft.Windows.PeopleExperienceHost_cw5n1h2txyewy", 1});
    CHECK(app.paths == std::vector<std::wstring>{L"Windows\\SystemApps\\Microsoft.Windows.PeopleExperienceHost_cw5n1h2txyewy"});
    REQUIRE(app.registry.size() == 2);
    CHECK(app.registry[0].kind == RegistryWrite::Kind::CreateKey);
    CHECK(app.registry[0].key.ends_with(L"\\Deprovisioned\\Microsoft.Windows.PeopleExperienceHost_cw5n1h2txyewy"));
    CHECK(app.registry[1].kind == RegistryWrite::Kind::DeleteKey);
    CHECK(validateComponentRecipe(app));

    // Driver: one package, deep-removed; the reference tool's row name; the recipe travels as JSON.
    const auto driver = driverRecipe({L"mdm5674a.inf_amd64_0123456789abcdef", L"mdm5674a.inf", L"{4D36E96D-E325-11CE-BFC1-08002BE10318}",
                                      L"Modemler", L"Contoso", L"10.0.26100.1", L"06/21/2006", {}, 1, false});
    CHECK(driver.title == L"Contoso  |  mdm5674a.inf");
    CHECK(driver.driverPackages == std::vector<std::wstring>{L"mdm5674a.inf_amd64_0123456789abcdef"});
    CHECK(driver.afterUpdates);
    CHECK(validateComponentRecipe(driver));
    const auto back = componentRecipeFromJson(componentRecipeToJson(driver));
    REQUIRE(back);
    CHECK(*back == driver);
    ComponentRecipe bad = driver;
    bad.driverPackages = {L"..\\evil"};
    CHECK_FALSE(validateComponentRecipe(bad));
}
