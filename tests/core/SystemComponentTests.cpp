// P07 system components (D-031): recipes, what a recipe may never touch, presence / size, forced
// removal that stays inside the tree, CBS package selection and unlocking, the store cleanup's
// command line and progress, and where these operations land in the plan.
#include "base/Utf8.h"
#include "core/image/SystemComponents.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/ops/Planner.h"
#include "core/system/FileLocks.h"

#include <doctest.h>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

using namespace wl;
using namespace wl::core;
using ops::ChangeSet;
using ops::OpKind;
using ops::Operation;
namespace fs = std::filesystem;

namespace {

// A fresh folder under the temp directory (removed first: leftovers of an aborted run).
fs::path scratch(const wchar_t* name) {
    const auto dir = fs::temp_directory_path() / L"wl-tests" / L"components" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void file(const fs::path& path, std::size_t bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << std::string(bytes, 'x');
}

// A directory junction (no privilege needed, unlike a symlink).
bool junction(const fs::path& link, const fs::path& target) {
    const std::wstring command = std::format(L"cmd /c mklink /J \"{}\" \"{}\" >nul 2>&1", link.wstring(), target.wstring());
    return _wsystem(command.c_str()) == 0;
}

ComponentRecipe oneDrive() {
    ComponentRecipe recipe;
    recipe.title = L"OneDrive kurulumu";
    recipe.packages = {L"Microsoft-Windows-OneDrive-Setup-Package", L"Microsoft-Windows-OneDrive-Setup-WOW64-Package"};
    recipe.paths = {L"Windows\\System32\\OneDriveSetup.exe"};
    recipe.registry.push_back(*registryWriteFrom(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run::OneDriveSetup", L"-"));
    recipe.registry.push_back(*registryWriteFrom(L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\\\::", L"[-]"));
    return recipe;
}

// The OneDrive packages of Windows 11 25H2 (26200.8037, tr-TR) as its SOFTWARE hive lists them,
// plus neighbours that must not be picked.
std::vector<CbsPackage> realPackages() {
    return {
        {L"Microsoft-Windows-OneDrive-Setup-Package~31bf3856ad364e35~amd64~tr-TR~10.0.26100.1", 2, 112},
        {L"Microsoft-Windows-OneDrive-Setup-Package~31bf3856ad364e35~amd64~~10.0.26100.1", 2, 64},
        {L"Microsoft-Windows-OneDrive-Setup-Package~31bf3856ad364e35~amd64~~10.0.26100.5074", 2, 112},
        {L"Microsoft-Windows-OneDrive-Setup-WOW64-Package~31bf3856ad364e35~amd64~tr-TR~10.0.26100.1", 2, 112},
        {L"Microsoft-Windows-OneDrive-Setup-WOW64-Package~31bf3856ad364e35~amd64~~10.0.26100.1", 2, 112},
        {L"Microsoft-Windows-OfflineFiles-Package~31bf3856ad364e35~amd64~~10.0.26100.8036", 2, 112},
        {L"Microsoft-Windows-StepsRecorder-Package~31bf3856ad364e35~amd64~~10.0.26100.8036", 1, 112},
        {L"Microsoft-Windows-OneDrive-Setup-Package-Extra~31bf3856ad364e35~amd64~~10.0.26100.1", 2, 112},
        {L"Microsoft-Windows-OneDrive-Setup-Package~31bf3856ad364e35~amd64~~10.0.26100.9", 2, 0x20}, // resolved only
    };
}

} // namespace

TEST_CASE("component recipe: JSON round trip, and the title the queue shows") {
    const ComponentRecipe recipe = oneDrive();
    const std::string json = componentRecipeToJson(recipe);
    const auto back = componentRecipeFromJson(json);
    REQUIRE(back);
    CHECK(*back == recipe);
    CHECK(back->registry[0].kind == RegistryWrite::Kind::DeleteValue);
    CHECK(back->registry[1].kind == RegistryWrite::Kind::DeleteKey);
    CHECK(componentTitle(std::wstring(json.begin(), json.end())) == L"OneDrive kurulumu");
    CHECK(componentTitle(L"not json").empty());
    CHECK(componentTitle(L"{\"resetBase\":true}").empty());
    CHECK_FALSE(componentRecipeFromJson("[1,2]"));
    CHECK_FALSE(componentRecipeFromJson(R"({"paths":[1]})"));
    CHECK_FALSE(componentRecipeFromJson(R"({"registry":[{"target":"HKXX\\a::b","value":"-"}]})"));
}

TEST_CASE("component recipe: what a preset must never be able to remove") {
    CHECK(validateComponentRecipe(oneDrive()));
    auto withPath = [](const wchar_t* path) {
        ComponentRecipe recipe;
        recipe.paths = {path};
        return validateComponentRecipe(recipe);
    };
    CHECK(withPath(L"Program Files (x86)\\Microsoft\\Edge"));
    CHECK(withPath(L"Windows/System32/Recovery/Winre.wim"));
    CHECK_FALSE(withPath(L""));
    CHECK_FALSE(withPath(L"Windows"));                        // too close to the root
    CHECK_FALSE(withPath(L"C:\\Windows\\Temp"));              // absolute
    CHECK_FALSE(withPath(L"\\Windows\\Temp"));
    CHECK_FALSE(withPath(L"Windows\\..\\..\\Users\\me"));     // out of the image
    CHECK_FALSE(withPath(L"Windows\\.\\Temp"));
    CHECK_FALSE(withPath(L"Windows\\\\Temp"));
    CHECK_FALSE(withPath(L"Windows\\Temp\\"));
    CHECK_FALSE(withPath(L"Windows\\Temp\\a.txt:stream"));
    CHECK_FALSE(withPath(L"Windows\\Temp\\*"));
    CHECK_FALSE(withPath(L"Windows\\System32"));              // Windows itself
    CHECK_FALSE(withPath(L"WINDOWS\\winsxs"));
    CHECK_FALSE(withPath(L"Windows/System32/config"));
    CHECK_FALSE(withPath(L"Users\\Default"));
    CHECK_FALSE(validateComponentRecipe({}));                 // does nothing

    ComponentRecipe recipe;
    recipe.packages = {L"Microsoft-Windows-OneDrive-Setup-Package~31bf"};
    CHECK_FALSE(validateComponentRecipe(recipe)); // an identity, not a family
    recipe.packages = {L""};
    CHECK_FALSE(validateComponentRecipe(recipe));
    recipe.packages.clear();
    RegistryWrite sam;
    sam.kind = RegistryWrite::Kind::DeleteKey;
    sam.key = L"HKLM\\SAM\\SAM";
    recipe.registry = {sam};
    CHECK_FALSE(validateComponentRecipe(recipe)); // no such hive to write
}

TEST_CASE("component recipe: the TroubleShooting package is never removed; an old telemetry recipe switches services off (D-075)") {
    ComponentRecipe recipe;
    recipe.packages = {L"Microsoft-OneCore-TroubleShooting-Package"};
    CHECK_FALSE(validateComponentRecipe(recipe)); // the first boot hangs without it
    recipe.packages = {L"microsoft-onecore-troubleshooting-wow64-package"};
    CHECK_FALSE(validateComponentRecipe(recipe));

    // The value a preset saved before D-075 carries (the user's "Windows 11 Home Single Language").
    const auto old = componentRecipeFromJson(
        R"({"packages":["Microsoft-OneCore-TroubleShooting-Package","Microsoft-OneCore-TroubleShooting-WOW64-Package"],)"
        R"x("title":"Telemetri ve tanılama (DiagTrack)"})x");
    REQUIRE(old);
    CHECK(old->packages.empty());
    REQUIRE(old->registry.size() == 3);
    CHECK(old->registry[0].key == L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\DiagTrack");
    CHECK(old->registry[0].name == L"Start");
    CHECK(old->registry[1].key == L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\dmwappushservice");
    CHECK(old->registry[2].name == L"AllowTelemetry");
    CHECK(validateComponentRecipe(*old));

    // Other packages of the same recipe stay; the writes are not added twice.
    const auto mixed = componentRecipeFromJson(utf8::fromWide(
        utf8::toWide(componentRecipeToJson(*old)).insert(1, L"\"packages\":[\"Microsoft-OneCore-TroubleShooting-Package\","
                                                            L"\"Microsoft-Windows-Help-ClientUA-Client-Package\"],")));
    REQUIRE(mixed);
    CHECK(mixed->packages == std::vector<std::wstring>{L"Microsoft-Windows-Help-ClientUA-Client-Package"});
    CHECK(mixed->registry.size() == 3);
}

TEST_CASE("component paths stay inside the image: a link on the way is refused") {
    const fs::path image = scratch(L"image");
    const fs::path outside = scratch(L"outside");
    file(image / L"Program Files" / L"App" / L"app.exe", 10);
    file(outside / L"Users" / L"keep.txt", 5);
    REQUIRE(junction(image / L"Documents and Settings", outside / L"Users"));

    const auto plain = resolveImagePath(image, L"Program Files\\App");
    REQUIRE(plain);
    CHECK(*plain == image / L"Program Files" / L"App");
    CHECK(resolveImagePath(image, L"Program Files/App/missing.dll")); // need not exist
    // Through the junction: that is the host's folder.
    CHECK_FALSE(resolveImagePath(image, L"Documents and Settings\\keep.txt"));
    CHECK_FALSE(resolveImagePath(image, L"..\\outside\\Users"));
}

TEST_CASE("component presence and size; forced removal takes links as links") {
    const fs::path image = scratch(L"image2");
    const fs::path outside = scratch(L"outside2");
    file(image / L"Program Files (x86)" / L"Microsoft" / L"Edge" / L"Application" / L"msedge.dll", 3000);
    file(image / L"Program Files (x86)" / L"Microsoft" / L"Edge" / L"Application" / L"1.0" / L"msedge.exe", 500);
    file(image / L"Windows" / L"System32" / L"OneDriveSetup.exe", 700);
    file(outside / L"host.txt", 9);
    // A link inside the component pointing out of the image.
    REQUIRE(junction(image / L"Program Files (x86)" / L"Microsoft" / L"Edge" / L"Link", outside));

    ComponentRecipe edge;
    edge.paths = {L"Program Files (x86)\\Microsoft\\Edge", L"Program Files (x86)\\Microsoft\\EdgeCore"};
    const ComponentPresence found = probeComponent(image, edge);
    CHECK(found.present);
    CHECK(found.size == 3500); // the junction's target is not counted
    ComponentRecipe absent;
    absent.paths = {L"Windows\\System32\\Recovery\\Winre.wim"};
    CHECK_FALSE(probeComponent(image, absent).present);
    ComponentRecipe single;
    single.paths = {L"Windows\\System32\\OneDriveSetup.exe"};
    CHECK(probeComponent(image, single).size == 700);
    // The path itself a junction: there, but nothing behind it is counted.
    ComponentRecipe link;
    link.paths = {L"Program Files (x86)\\Microsoft\\Edge\\Link"};
    CHECK(probeComponent(image, link).present);
    CHECK(probeComponent(image, link).size == 0);

    const fs::path target = *resolveImagePath(image, L"Program Files (x86)\\Microsoft\\Edge");
    SetFileAttributesW((target / L"Application" / L"msedge.dll").c_str(), FILE_ATTRIBUTE_READONLY);
    REQUIRE(forceRemoveEntry(target));
    CHECK_FALSE(fs::exists(target));
    CHECK(fs::exists(image / L"Program Files (x86)" / L"Microsoft")); // the parent stays
    CHECK(fs::exists(outside / L"host.txt"));                          // nothing behind the link was touched
    CHECK(forceRemoveEntry(target));                                   // already gone: fine
    REQUIRE(forceRemoveEntry(*resolveImagePath(image, L"Windows\\System32\\OneDriveSetup.exe")));
    CHECK_FALSE(probeComponent(image, single).present);
    CHECK_FALSE(forceRemoveEntry(L"relative\\path"));
}

TEST_CASE("CBS: families, language packages first, installed before staged, neighbours left alone") {
    CHECK(cbsPackageFamily(L"Microsoft-Windows-OneDrive-Setup-Package~31bf3856ad364e35~amd64~~10.0.26100.5074") ==
          L"Microsoft-Windows-OneDrive-Setup-Package");
    CHECK(cbsPackageFamily(L"NoTilde") == L"NoTilde");
    CHECK(cbsLanguageNeutral(L"A~31bf3856ad364e35~amd64~~10.0.1"));
    CHECK_FALSE(cbsLanguageNeutral(L"A~31bf3856ad364e35~amd64~tr-TR~10.0.1"));

    const auto order = cbsRemovalOrder(oneDrive().packages, realPackages());
    REQUIRE(order.size() == 5);
    CHECK_FALSE(cbsLanguageNeutral(order[0].identity));
    CHECK_FALSE(cbsLanguageNeutral(order[1].identity));
    CHECK(cbsLanguageNeutral(order[2].identity));
    CHECK(order[2].state == kCbsInstalled);
    CHECK(order[3].state == kCbsInstalled);
    CHECK(order[4].state == kCbsStaged); // the older version's leftover goes last
    for (const auto& package : order) {
        CHECK(package.identity.find(L"OneDrive-Setup") != std::wstring::npos);
        CHECK(package.identity.find(L"Extra") == std::wstring::npos); // a longer family name is another package
        CHECK(package.state >= kCbsStaged);
    }
    // Family names compare without case.
    CHECK(cbsRemovalOrder({L"microsoft-windows-onedrive-setup-package"}, realPackages()).size() == 3);
    CHECK(cbsRemovalOrder({}, realPackages()).empty());
}

TEST_CASE("CBS: reading the package keys and unlocking one (Visibility = 1, no Owners)") {
    // The same layout as "…\Component Based Servicing\Packages", in a scratch key of this user.
    const std::wstring base = std::format(L"Software\\WinLove\\Tests\\cbs-{}", GetCurrentProcessId());
    RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
    HKEY rawRoot = nullptr;
    REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &rawRoot, nullptr) ==
            ERROR_SUCCESS);
    {
        const RegKey root(rawRoot);
        for (const auto& package : realPackages()) {
            HKEY raw = nullptr;
            REQUIRE(RegCreateKeyExW(root.get(), package.identity.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &raw,
                                    nullptr) == ERROR_SUCCESS);
            RegKey key(raw);
            REQUIRE(key.setDword(L"Visibility", package.visibility));
            REQUIRE(key.setDword(L"CurrentState", package.state));
            HKEY owners = nullptr;
            REQUIRE(RegCreateKeyExW(key.get(), L"Owners", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &owners, nullptr) ==
                    ERROR_SUCCESS);
            RegKey ownersKey(owners);
            REQUIRE(ownersKey.setDword(L"Microsoft-Windows-Not-Supported-On-LTSB-Package~31bf3856ad364e35~amd64~~10.0.26100.5074",
                                       196720));
        }

        const auto read = readCbsPackages(root.get());
        REQUIRE(read.size() == realPackages().size());
        const auto order = cbsRemovalOrder(oneDrive().packages, read);
        REQUIRE(order.size() == 5);
        for (const auto& package : order) {
            REQUIRE(unlockCbsPackage(root.get(), package.identity));
        }
        CHECK(unlockCbsPackage(root.get(), order.front().identity)); // again: nothing left to do
        CHECK_FALSE(unlockCbsPackage(root.get(), L"No-Such-Package~31bf3856ad364e35~amd64~~1.0"));

        const auto before = realPackages();
        for (const auto& package : readCbsPackages(root.get())) {
            const bool target = std::ranges::any_of(order, [&](const CbsPackage& p) { return p.identity == package.identity; });
            CHECK(package.visibility ==
                  (target ? 1u : std::ranges::find(before, package.identity, &CbsPackage::identity)->visibility));
            HKEY owners = nullptr;
            const LSTATUS status =
                RegOpenKeyExW(root.get(), (package.identity + L"\\Owners").c_str(), 0, KEY_READ, &owners);
            if (owners) {
                RegCloseKey(owners);
            }
            CHECK(status == (target ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS)); // others keep their owners
        }
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
    // Leave nothing behind: the parents go too when they are empty (RegDeleteKey refuses otherwise).
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\WinLove\\Tests");
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\WinLove");
}

TEST_CASE("store cleanup: options, command line, progress out of dism.exe's output") {
    const StoreCleanupOptions options{L"Bileşen deposu temizliği (ResetBase)", true};
    const auto back = storeCleanupFromJson(storeCleanupToJson(options));
    REQUIRE(back);
    CHECK(*back == options);
    CHECK(storeCleanupFromJson(R"({"title":"x"})")->resetBase); // the default
    CHECK_FALSE(storeCleanupFromJson(R"({"resetBase":false})")->resetBase);
    CHECK_FALSE(storeCleanupFromJson("nope"));
    const std::string json = storeCleanupToJson(options);
    CHECK(componentTitle(utf8::toWide(json)) == options.title);

    CHECK(storeCleanupCommandLine(L"C:\\Windows\\System32\\dism.exe", L"D:\\WinLove Work\\mount\\", true) ==
          L"\"C:\\Windows\\System32\\dism.exe\" /English /Image:\"D:\\WinLove Work\\mount\" /Cleanup-Image "
          L"/StartComponentCleanup /ResetBase");
    CHECK(storeCleanupCommandLine(L"dism.exe", L"C:\\m", false).ends_with(L"/StartComponentCleanup"));

    CHECK(lastDismPercent("[=====                      10.0%                           ]") == doctest::Approx(0.10));
    CHECK(lastDismPercent("\r[==  5.0%  ]\r[=====  12.5%  ]") == doctest::Approx(0.125));
    CHECK(lastDismPercent("[==========================100.0%==========================]") == doctest::Approx(1.0));
    CHECK_FALSE(lastDismPercent("Deployment Image Servicing and Management tool"));
    CHECK_FALSE(lastDismPercent("%"));
    CHECK_FALSE(lastDismPercent(""));
    CHECK(lastDismPercent("Error: 0x800f0806 %1 20.0%") == doctest::Approx(0.20));
}

TEST_CASE("plan: component removals with the other removals, the store cleanup right after the updates") {
    ChangeSet set;
    set.add({OpKind::SetRegistryValue, L"HKLM\\SOFTWARE\\X::y", L"dword:00000001"});
    set.add({OpKind::RemoveAppx, L"App_1.0_x64__abc"});
    set.add({OpKind::RemoveComponent, L"onedrive", utf8::toWide(componentRecipeToJson(oneDrive())), ops::Risk::Low, -700});
    set.add({OpKind::CleanupImage, L"component-store", utf8::toWide(storeCleanupToJson({L"Temizlik", true}))});
    set.add({OpKind::DisableFeature, L"F1"});
    auto plan = ops::plan(set);
    REQUIRE(plan.steps.size() == 5);
    CHECK(plan.steps[0].operation.kind == OpKind::RemoveAppx);
    CHECK(plan.steps[1].operation.kind == OpKind::RemoveComponent);
    CHECK(plan.steps[1].phase == ops::Phase::Remove);
    CHECK(plan.steps[2].operation.kind == OpKind::DisableFeature);
    CHECK(plan.steps[3].operation.kind == OpKind::CleanupImage);
    CHECK(plan.steps[3].phase == ops::Phase::Cleanup);
    CHECK(plan.steps[4].phase == ops::Phase::Settings);
    // Nothing was added: seconds (13 s in the lab). With an update in the run: what it superseded goes.
    CHECK(ops::estimateSeconds(plan, 3) < 60);
    const auto quick = ops::groups(plan);
    REQUIRE(quick.size() == 4);
    CHECK(quick[2].phase == ops::Phase::Cleanup);

    set.add({OpKind::AddPackage, L"C:\\updates\\lcu.msu", L"lcu"});
    plan = ops::plan(set);
    REQUIRE(plan.steps.size() == 6);
    CHECK(plan.steps[3].operation.kind == OpKind::AddPackage);
    CHECK(plan.steps[4].operation.kind == OpKind::CleanupImage); // after the update, before the registry
    CHECK(plan.steps[5].phase == ops::Phase::Settings);
    CHECK(ops::estimateSeconds(plan, 4) >= 300);
    CHECK(ops::groups(plan)[3].estimateSeconds == doctest::Approx(ops::estimateSeconds(plan, 4)));

    // Presets carry both kinds, recipe included.
    const auto back = ChangeSet::fromJson(set.toJson());
    REQUIRE(back);
    const auto* component = back->find(OpKind::RemoveComponent, L"onedrive");
    REQUIRE(component);
    CHECK(*componentRecipeFromJson(utf8::fromWide(component->value)) == oneDrive());
    CHECK(component->sizeDelta == -700);
    CHECK(back->find(OpKind::CleanupImage, L"component-store"));
}

TEST_CASE("the recipes tools/lab_components.ps1 feeds to wlcli are valid") {
    for (const wchar_t* name : {L"recipe-onedrive.json", L"recipe-edge.json", L"recipe-winre.json"}) {
        CAPTURE(name);
        std::ifstream in(fs::path(WL_SOURCE_DIR) / L"tests/integration/fixtures" / name, std::ios::binary);
        REQUIRE(in);
        const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto recipe = componentRecipeFromJson(json);
        REQUIRE(recipe);
        CHECK(validateComponentRecipe(*recipe));
        CHECK_FALSE(recipe->title.empty());
        CHECK_FALSE(recipe->paths.empty());
    }
}
