// D-154 Grup İlkesi and D-155 Sağ tık menüsü: the ADMX / ADML reader on a small template of our own
// (namespaces across files, presentations, string refs), the writes of each element kind, Registry.pol
// in and out, gpt.ini, the context menu's writes, and the operations' JSON. The real image's templates
// were read in build\lab\d154 (3532 policies, 662 ms).
#include "app/controllers/ContextMenuController.h"
#include "app/controllers/PolicyController.h"
#include "base/File.h"
#include "core/image/ContextMenu.h"
#include "core/policy/PolicyWrites.h"

#include <doctest.h>

#include <windows.h>

#include <filesystem>

using namespace wl;
using namespace wl::core;
using namespace wl::core::policy;

namespace {

constexpr std::string_view kWindowsAdmx = R"xml(<?xml version="1.0" encoding="utf-8"?>
<policyDefinitions revision="1.0" schemaVersion="1.0">
  <policyNamespaces><target prefix="windows" namespace="Microsoft.Policies.Windows" /></policyNamespaces>
  <supportedOn><definitions><definition name="SUPPORTED_Win10" displayName="$(string.SUPPORTED_Win10)" /></definitions></supportedOn>
  <categories><category name="WindowsComponents" displayName="$(string.WindowsComponents)" /></categories>
</policyDefinitions>)xml";

constexpr std::string_view kWindowsAdml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<policyDefinitionResources revision="1.0" schemaVersion="1.0"><resources><stringTable>
  <string id="SUPPORTED_Win10">En az Windows 10</string>
  <string id="WindowsComponents">Windows Bileşenleri</string>
</stringTable></resources></policyDefinitionResources>)xml";

constexpr std::string_view kTestAdmx = R"xml(<?xml version="1.0" encoding="utf-8"?>
<policyDefinitions xmlns="http://schemas.microsoft.com/GroupPolicy/2006/07/PolicyDefinitions" revision="1.0" schemaVersion="1.0">
  <policyNamespaces>
    <target prefix="test" namespace="WinLove.Policies.Test" />
    <using prefix="windows" namespace="Microsoft.Policies.Windows" />
  </policyNamespaces>
  <categories>
    <category name="TestCat" displayName="$(string.TestCat)"><parentCategory ref="windows:WindowsComponents" /></category>
  </categories>
  <policies>
    <policy name="Simple" class="Machine" displayName="$(string.Simple)" explainText="$(string.Simple_Help)" key="Software\Policies\Test" valueName="NoThing">
      <parentCategory ref="TestCat" /><supportedOn ref="windows:SUPPORTED_Win10" />
    </policy>
    <policy name="Inverted" class="Both" displayName="$(string.Inverted)" key="Software\Policies\Test" valueName="Allow">
      <parentCategory ref="TestCat" />
      <enabledValue><decimal value="0" /></enabledValue>
      <disabledValue><decimal value="1" /></disabledValue>
      <enabledList><item key="Software\Policies\Test\Extra" valueName="Mode"><value><string>strict</string></value></item></enabledList>
    </policy>
    <policy name="Options" class="Machine" displayName="$(string.Options)" presentation="$(presentation.Options)" key="Software\Policies\Test\Opt">
      <parentCategory ref="TestCat" />
      <elements>
        <enum id="Level" valueName="Level" required="true">
          <item displayName="$(string.Low)"><value><decimal value="1" /></value></item>
          <item displayName="$(string.High)"><value><decimal value="3" /></value><valueList><item valueName="Audit"><value><decimal value="1" /></value></item></valueList></item>
        </enum>
        <boolean id="Flag" valueName="Flag"><trueValue><decimal value="7" /></trueValue><falseValue><delete /></falseValue></boolean>
        <decimal id="Days" valueName="Days" minValue="1" maxValue="30" />
        <text id="Server" valueName="Server" expandable="true" />
        <list id="Hosts" key="Software\Policies\Test\Opt\Hosts" valuePrefix="" />
        <list id="Named" key="Software\Policies\Test\Opt\Named" explicitValue="true" />
        <multiText id="Lines" valueName="Lines" />
      </elements>
    </policy>
  </policies>
</policyDefinitions>)xml";

constexpr std::string_view kTestAdml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<policyDefinitionResources revision="1.0" schemaVersion="1.0"><resources>
  <stringTable>
    <string id="TestCat">Deneme</string>
    <string id="Simple">Basit ilke</string>
    <string id="Simple_Help">Ne yaptığı.</string>
    <string id="Inverted">Ters ilke</string>
    <string id="Options">Seçenekli ilke</string>
    <string id="Low">Düşük</string>
    <string id="High">Yüksek</string>
  </stringTable>
  <presentationTable>
    <presentation id="Options">
      <text>Açıklama</text>
      <dropdownList refId="Level" defaultItem="1">Düzey:</dropdownList>
      <checkBox refId="Flag" defaultChecked="true">Bayrak</checkBox>
      <decimalTextBox refId="Days" defaultValue="7">Gün:</decimalTextBox>
      <textBox refId="Server"><label>Sunucu:</label><defaultValue>%ComputerName%</defaultValue></textBox>
      <listBox refId="Hosts">Adresler</listBox>
    </presentation>
  </presentationTable>
</resources></policyDefinitionResources>)xml";

PolicyCatalog testCatalog() {
    PolicyCatalog catalog;
    REQUIRE(catalog.add(kWindowsAdmx, kWindowsAdml, L"Windows.admx"));
    REQUIRE(catalog.add(kTestAdmx, kTestAdml, L"Test.admx"));
    catalog.finish();
    return catalog;
}

std::wstring formatted(const std::vector<RegistryWrite>& writes) {
    std::wstring out;
    for (const auto& w : writes) {
        out += w.key + L"::" + w.name + L"=" + formatRegValue(w) + L"\n";
    }
    return out;
}

std::filesystem::path scratch(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"policy" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("policy: ADMX + ADML — ids, names, categories across files, supportedOn, presentation") {
    const auto catalog = testCatalog();
    REQUIRE(catalog.policies().size() == 3);
    const auto* simple = catalog.policy(L"WinLove.Policies.Test:Simple");
    REQUIRE(simple);
    CHECK(simple->displayName == L"Basit ilke");
    CHECK(simple->explain == L"Ne yaptığı.");
    CHECK(simple->supportedOn == L"En az Windows 10");
    CHECK(catalog.path(*simple) == std::vector<std::wstring>{L"Windows Bileşenleri", L"Deneme"});
    CHECK(catalog.rootOf(*simple) == L"Microsoft.Policies.Windows:WindowsComponents");
    const auto* options = catalog.policy(L"winlove.policies.test:options"); // ids compare without case
    REQUIRE(options);
    CHECK(options->elements.size() == 7);
    CHECK(options->presentation.size() == 6);
    CHECK(options->element(L"Level")->items[1].displayName == L"Yüksek");
    CHECK(catalog.policy(L"WinLove.Policies.Test:Inverted")->policyClass == PolicyClass::Both);
    // A broken file is refused, not fatal.
    PolicyCatalog other;
    CHECK_FALSE(other.add("<notAdmx/>", "", L"x.admx"));
}

TEST_CASE("policy: Enabled / Disabled writes — defaults 1 / 0, explicit values, lists, both scopes") {
    const auto catalog = testCatalog();
    const auto& simple = *catalog.policy(L"WinLove.Policies.Test:Simple");
    CHECK(formatted(*policyWrites(simple, false, {PolicyState::Enabled, {}})) == L"HKLM\\Software\\Policies\\Test::NoThing=dword:00000001\n");
    CHECK(formatted(*policyWrites(simple, false, {PolicyState::Disabled, {}})) == L"HKLM\\Software\\Policies\\Test::NoThing=dword:00000000\n");
    CHECK(policyWrites(simple, false, {PolicyState::NotConfigured, {}})->empty());
    const auto& inverted = *catalog.policy(L"WinLove.Policies.Test:Inverted");
    CHECK(formatted(*policyWrites(inverted, true, {PolicyState::Enabled, {}})) ==
          L"HKCU\\Software\\Policies\\Test::Allow=dword:00000000\nHKCU\\Software\\Policies\\Test\\Extra::Mode=\"strict\"\n");
    CHECK(formatted(*policyWrites(inverted, false, {PolicyState::Disabled, {}})) == L"HKLM\\Software\\Policies\\Test::Allow=dword:00000001\n");
}

TEST_CASE("policy: element values — defaults from the presentation, every kind, range checks") {
    const auto catalog = testCatalog();
    const auto& options = *catalog.policy(L"WinLove.Policies.Test:Options");
    auto values = defaultValues(options);
    CHECK(values[L"Level"] == L"1");
    CHECK(values[L"Flag"] == L"1");
    CHECK(values[L"Days"] == L"7");
    CHECK(values[L"Server"] == L"%ComputerName%");
    values[L"Hosts"] = L"a.example\nb.example";
    values[L"Named"] = L"one=1\ntwo = 2";
    values[L"Lines"] = L"x\ny";
    const auto writes = policyWrites(options, false, {PolicyState::Enabled, values});
    REQUIRE(writes);
    const std::wstring text = formatted(*writes);
    CHECK(text.find(L"Opt::Level=dword:00000003") != std::wstring::npos);
    CHECK(text.find(L"Opt::Audit=dword:00000001") != std::wstring::npos); // the item's valueList
    CHECK(text.find(L"Opt::Flag=dword:00000007") != std::wstring::npos);
    CHECK(text.find(L"Opt::Days=dword:00000007") != std::wstring::npos);
    CHECK(text.find(L"Opt::Server=hex(2):") != std::wstring::npos); // expandable
    CHECK(text.find(L"Opt\\Hosts::a.example=\"a.example\"") != std::wstring::npos);
    CHECK(text.find(L"Opt\\Named::two=\"2\"") != std::wstring::npos);
    CHECK(text.find(L"Opt::Lines=hex(7):") != std::wstring::npos);
    // Unchecked: the falseValue deletes.
    values[L"Flag"] = L"0";
    CHECK(formatted(*policyWrites(options, false, {PolicyState::Enabled, values})).find(L"Opt::Flag=-") != std::wstring::npos);
    // Out of range, not a number, a bad choice, a required value missing.
    values[L"Days"] = L"31";
    CHECK_FALSE(policyWrites(options, false, {PolicyState::Enabled, values}));
    values[L"Days"] = L"seven";
    CHECK_FALSE(policyWrites(options, false, {PolicyState::Enabled, values}));
    values[L"Days"] = L"7";
    values[L"Level"] = L"5";
    CHECK_FALSE(policyWrites(options, false, {PolicyState::Enabled, values}));
    values[L"Level"] = L"";
    CHECK(missingValues(options, values) == std::vector<std::wstring>{L"Level"});
    // Disabled: the elements' values deleted (lists left: their names are the entries).
    const std::wstring disabled = formatted(*policyWrites(options, false, {PolicyState::Disabled, {}}));
    CHECK(disabled.find(L"Opt::Level=-") != std::wstring::npos);
    CHECK(disabled.find(L"Hosts") == std::wstring::npos);
}

TEST_CASE("policy: the operation's JSON round-trips, targets parse") {
    const auto catalog = testCatalog();
    const auto& options = *catalog.policy(L"WinLove.Policies.Test:Options");
    PolicyChoice choice{PolicyState::Enabled, defaultValues(options)};
    auto op = app::PolicyController::operationFor(options, false, choice);
    REQUIRE(op);
    CHECK(op->kind == ops::OpKind::SetPolicy);
    CHECK(op->target == L"machine\\WinLove.Policies.Test:Options");
    const auto back = parsePolicyOperation(op->value);
    REQUIRE(back);
    CHECK(back->name == L"Seçenekli ilke");
    CHECK(back->choice == choice);
    CHECK(back->writes == *policyWrites(options, false, choice));
    std::wstring id;
    bool user = false;
    CHECK(parsePolicyTarget(L"user\\A:B", id, user));
    CHECK((id == L"A:B" && user));
    CHECK_FALSE(parsePolicyTarget(L"other\\A:B", id, user));
}

TEST_CASE("policy: Registry.pol — format, parse, merge (deletes as **del.), gpt.ini") {
    const auto catalog = testCatalog();
    const auto& inverted = *catalog.policy(L"WinLove.Policies.Test:Inverted");
    std::vector<PolEntry> entries;
    for (const auto& w : *policyWrites(inverted, false, {PolicyState::Enabled, {}})) {
        mergeIntoPol(entries, w);
    }
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].key == L"Software\\Policies\\Test");
    CHECK(entries[0].type == REG_DWORD);
    const std::string bytes = formatPol(entries);
    CHECK(bytes.substr(0, 4) == "PReg");
    const auto parsed = parsePol(bytes);
    REQUIRE(parsed);
    CHECK(*parsed == entries);
    // The same value again replaces; a delete becomes **del.<name> in its place.
    RegistryWrite remove;
    remove.kind = RegistryWrite::Kind::DeleteValue;
    remove.key = L"HKLM\\Software\\Policies\\Test";
    remove.name = L"Allow";
    mergeIntoPol(entries, remove);
    CHECK(entries.size() == 2);
    CHECK(entries.back().valueName == L"**del.Allow");
    CHECK_FALSE(parsePol("PReg\x02\0\0\0"));

    // Into a folder: Machine + User files and gpt.ini with both versions, other tools' lines kept.
    const auto mount = scratch(L"gpo");
    const auto gpo = mount / L"Windows" / L"System32" / L"GroupPolicy";
    std::filesystem::create_directories(gpo);
    REQUIRE(writeFileAtomic(gpo / L"gpt.ini", "[General]\r\ngPCMachineExtensionNames=[{42B5FAAE-6536-11D2-AE5A-0000F87571E3}{40B6664F-4972-11D1-A7CA-0000F87571E3}]\r\nVersion=3\r\n"));
    auto writes = *policyWrites(inverted, false, {PolicyState::Enabled, {}});
    const auto userWrites = *policyWrites(inverted, true, {PolicyState::Disabled, {}});
    writes.insert(writes.end(), userWrites.begin(), userWrites.end());
    REQUIRE(recordInGroupPolicy(mount, writes));
    CHECK(parsePol(*readFileBytes(gpo / L"Machine" / L"Registry.pol"))->size() == 2);
    CHECK(parsePol(*readFileBytes(gpo / L"User" / L"Registry.pol"))->size() == 1);
    const std::string ini = *readFileBytes(gpo / L"gpt.ini");
    CHECK(ini.find("Version=65540") != std::string::npos); // user 1 << 16 | machine 3 + 1
    CHECK(ini.find("gPCMachineExtensionNames=[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}{D02B1F72-3407-48AE-BA88-E8213C6761F1}][{42B5FAAE") !=
          std::string::npos);
    CHECK(ini.find("gPCUserExtensionNames=[{35378EAC") != std::string::npos);
    std::filesystem::remove_all(mount);
}

TEST_CASE("context menu: hide / show writes, our item's keys (%1 → %V on a background), ids") {
    ContextMenuEntry verb;
    verb.kind = ContextMenuEntry::Kind::Verb;
    verb.id = L"Directory\\Background\\shell\\cmd";
    CHECK(formatted(contextMenuHideWrites(verb)) == L"HKLM\\SOFTWARE\\Classes\\Directory\\Background\\shell\\cmd::ProgrammaticAccessOnly=\"\"\n");
    CHECK(contextMenuShowWrites(verb).empty()); // not hidden: nothing to show
    ContextMenuEntry handler;
    handler.kind = ContextMenuEntry::Kind::Handler;
    handler.id = handler.clsid = L"{596AB062-B4D2-4215-9F74-E9109B0A8153}";
    handler.name = L"Önceki sürümler";
    handler.hidden = true;
    CHECK(contextMenuHideWrites(handler).empty());
    CHECK(formatted(contextMenuShowWrites(handler)) ==
          L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked::{596AB062-B4D2-4215-9F74-E9109B0A8153}=-\n");

    ContextMenuItem item;
    item.name = L"Open with VS Code";
    item.id = contextMenuItemId(item.name);
    CHECK(item.id == L"open-with-vs-code");
    CHECK(contextMenuItemId(L"Şş Çç!!") == L"item"); // nothing ASCII left
    item.command = L"code.exe \"%1\"";
    item.places = {L"*", L"Directory\\Background"};
    item.extended = true;
    const auto writes = contextMenuAddWrites(item);
    REQUIRE(writes);
    const std::wstring text = formatted(*writes);
    CHECK(text.find(L"HKLM\\SOFTWARE\\Classes\\*\\shell\\WinLove.open-with-vs-code::=\"Open with VS Code\"") != std::wstring::npos);
    CHECK(text.find(L"WinLove.open-with-vs-code::Extended=\"\"") != std::wstring::npos);
    CHECK((*writes)[2].type == REG_EXPAND_SZ);
    // The background's command says %V.
    const auto& background = writes->back();
    CHECK(background.key == L"HKLM\\SOFTWARE\\Classes\\Directory\\Background\\shell\\WinLove.open-with-vs-code\\command");
    CHECK(formatRegValue(background) == formatRegValue([] {
              RegistryWrite w;
              w.type = REG_EXPAND_SZ;
              for (const wchar_t c : std::wstring_view(L"code.exe \"%V\"")) {
                  w.data.push_back(static_cast<std::uint8_t>(c & 0xFF));
                  w.data.push_back(static_cast<std::uint8_t>(c >> 8));
              }
              w.data.push_back(0);
              w.data.push_back(0);
              return w;
          }()));
    // Refused: no command, an unknown place, a bad id.
    auto bad = item;
    bad.command.clear();
    CHECK_FALSE(contextMenuAddWrites(bad));
    bad = item;
    bad.places = {L"HKLM\\Evil"};
    CHECK_FALSE(contextMenuAddWrites(bad));
    bad = item;
    bad.id = L"..\\x";
    CHECK_FALSE(contextMenuAddWrites(bad));

    // JSON round trip.
    ContextMenuOperation op{item.name, item, *writes};
    const auto back = parseContextMenuOperation(contextMenuOperationJson(op));
    REQUIRE(back);
    CHECK(back->item == item);
    CHECK(back->writes == *writes);
}

TEST_CASE("context menu: Windows 11 packaged items from the manifests (CLSIDs without braces)") {
    const auto mount = scratch(L"packaged");
    const auto pkg = mount / L"Program Files" / L"WindowsApps" / L"Microsoft.WindowsTerminal_1.21.0.0_x64__8wekyb3d8bbwe";
    std::filesystem::create_directories(pkg);
    std::filesystem::create_directories(mount / L"Program Files" / L"WindowsApps" / L"Microsoft.WindowsTerminal_1.21.0.0_neutral_~_8wekyb3d8bbwe");
    REQUIRE(writeFileAtomic(pkg / L"AppxManifest.xml", R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:desktop4="http://schemas.microsoft.com/appx/manifest/desktop/windows10/4"
         xmlns:desktop5="http://schemas.microsoft.com/appx/manifest/desktop/windows10/5">
  <Identity Name="Microsoft.WindowsTerminal" Publisher="CN=Microsoft" Version="1.21.0.0" />
  <Applications><Application Id="App"><Extensions>
    <desktop4:Extension Category="windows.fileExplorerContextMenus"><desktop4:FileExplorerContextMenus>
      <desktop5:ItemType Type="Directory"><desktop5:Verb Id="OpenTerminal" Clsid="9f156763-7844-4dc4-b2b1-901f640f5155" /></desktop5:ItemType>
      <desktop5:ItemType Type="Directory\Background"><desktop5:Verb Id="OpenTerminal" Clsid="9f156763-7844-4dc4-b2b1-901f640f5155" /></desktop5:ItemType>
    </desktop4:FileExplorerContextMenus></desktop4:Extension>
  </Extensions></Application></Applications>
</Package>)xml"));
    const auto entries = readPackagedContextMenu(mount);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].kind == ContextMenuEntry::Kind::Packaged);
    CHECK(entries[0].clsid == L"{9F156763-7844-4DC4-B2B1-901F640F5155}");
    CHECK(entries[0].keyName == L"OpenTerminal");
    CHECK(entries[0].name == L"OpenTerminal (WindowsTerminal)");
    CHECK(entries[0].source == L"Microsoft.WindowsTerminal");
    CHECK(entries[0].places == std::vector<std::wstring>{L"Directory", L"Directory\\Background"});
    CHECK(formatted(contextMenuHideWrites(entries[0])) ==
          L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked::{9F156763-7844-4DC4-B2B1-901F640F5155}=\"OpenTerminal (WindowsTerminal)\"\n");
    std::filesystem::remove_all(mount);
}

TEST_CASE("controllers: a policy's state through the queue; menu entries hidden, shown back, our items") {
    const auto dir = scratch(L"state");
    app::AppState state{dir / L"recent.json", dir / L"settings.json"};
    state.setMounted(app::MountedImage{dir, L"C:\\w\\install.wim", 1, L"Pro"});
    state.setPolicyList(app::AppState::PolicyList{app::AppState::PolicyList::Status::Ready, dir,
                                                  std::make_shared<const PolicyCatalog>(testCatalog()), {}});
    app::PolicyController policies(state, [](std::function<void()> f) { f(); });
    REQUIRE(policies.rows().size() == 4); // Simple, Inverted ×2 (Both), Options
    const auto row = policies.rows()[1];
    CHECK(policies.choice(row).state == PolicyState::NotConfigured);
    REQUIRE(policies.set(row, PolicyState::Disabled));
    CHECK(policies.choice(row).state == PolicyState::Disabled);
    CHECK(policies.changedCount() == 1);
    // Options: Enabled without values takes the defaults; a bad value queues nothing.
    const auto options = policies.rows()[3];
    CHECK_FALSE(policies.set(options, PolicyState::Enabled, PolicyValues{{L"Level", L"9"}}));
    REQUIRE(policies.set(options, PolicyState::Enabled));
    CHECK(policies.choice(options).values.at(L"Days") == L"7");
    REQUIRE(policies.set(row, PolicyState::NotConfigured));
    CHECK(policies.changedCount() == 1);
    policies.resetChanges();
    CHECK(state.changes().empty());

    app::ContextMenuController menu(state, [](std::function<void()> f) { f(); });
    ContextMenuEntry verb;
    verb.kind = ContextMenuEntry::Kind::Verb;
    verb.id = L"Drive\\shell\\encrypt-bde";
    menu.setHidden(verb, true);
    CHECK(menu.hidden(verb));
    menu.setHidden(verb, false); // back to the image's state: out of the queue
    CHECK(state.changes().empty());
    verb.hidden = true;          // one the image hides: showing it is a change
    menu.setHidden(verb, false);
    CHECK_FALSE(menu.hidden(verb));
    CHECK(menu.changedCount() == 1);
    ContextMenuItem item;
    item.name = L"Hash";
    item.command = L"certutil -hashfile \"%1\" SHA256";
    item.places = {L"*"};
    REQUIRE(menu.addItem(item));
    REQUIRE(menu.addItem(item)); // the same name again: its own id
    const auto items = menu.items();
    REQUIRE(items.size() == 2);
    CHECK(items[0].id == L"hash");
    CHECK(items[1].id == L"hash-2");
    item.command.clear();
    CHECK_FALSE(menu.addItem(item));
    menu.removeItem(L"hash");
    CHECK(menu.items().size() == 1);
    menu.resetChanges();
    CHECK(state.changes().empty());
    std::filesystem::remove_all(dir);
}
