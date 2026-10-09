// P13: answer file generation, reading it back, validation, Setup's password encoding.
#include "base/Utf8.h"
#include "core/unattend/Unattend.h"
#include "core/unattend/Welcome.h"

#include <doctest.h>

#include <algorithm>

using namespace wl;
using namespace wl::core;

namespace {

bool has(const std::wstring& xml, std::wstring_view fragment) {
    return xml.find(fragment) != std::wstring::npos;
}

UnattendOptions everything() {
    UnattendOptions o;
    o.architecture = Architecture::X64;
    o.uiLanguage = L"tr-TR";
    o.locale = L"tr-TR";
    o.keyboard = L"041f:0000041f";
    o.timeZone = L"Turkey Standard Time";
    o.accountName = L"admin";
    o.password = L"Parola123!";
    o.autoLogon = true;
    o.computerName = L"WINLOVE-PC";
    o.disk = UnattendDisk::WipeGpt;
    o.skipPrivacy = true;
    o.skipOnlineAccount = true;
    o.bypassNro = true;
    o.acceptEula = true;
    o.productKey = L"W269N-WFGWX-YVC9B-4J6C9-T83GX";
    o.imageIndex = 4;
    o.bypassTpm = true;
    o.bypassSecureBoot = true;
    o.bypassRam = true;
    o.bypassCpu = true;
    o.bypassStorage = true;
    return o;
}

} // namespace

TEST_CASE("unattend: no options → a valid file without settings") {
    const std::wstring xml = buildUnattendXml({});
    CHECK(xml.starts_with(L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<unattend xmlns=\"urn:schemas-microsoft-com:unattend\""));
    CHECK_FALSE(has(xml, L"<settings"));
    CHECK(xml.ends_with(L"</unattend>\n"));
    const auto back = parseUnattendXml(utf8::fromWide(xml));
    REQUIRE(back);
    CHECK(*back == UnattendOptions{});
}

TEST_CASE("unattend: only what an option asks for is written") {
    UnattendOptions o;
    o.bypassTpm = true;
    std::wstring xml = buildUnattendXml(o);
    CHECK(has(xml, L"<settings pass=\"windowsPE\">"));
    CHECK(has(xml, L"<component name=\"Microsoft-Windows-Setup\" processorArchitecture=\"amd64\" "
                   L"publicKeyToken=\"31bf3856ad364e35\" language=\"neutral\" versionScope=\"nonSxS\">"));
    CHECK(has(xml, L"<Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig /v BypassTPMCheck /t REG_DWORD /d 1 /f</Path>"));
    CHECK_FALSE(has(xml, L"BypassRAMCheck"));
    CHECK_FALSE(has(xml, L"specialize"));
    CHECK_FALSE(has(xml, L"oobeSystem"));
    CHECK_FALSE(has(xml, L"DiskConfiguration")); // the disk page stays: nothing is erased unasked
    CHECK_FALSE(has(xml, L"UserData"));

    o = {};
    o.architecture = Architecture::Arm64;
    o.accountName = L"R&D <team>";
    xml = buildUnattendXml(o);
    CHECK(has(xml, L"processorArchitecture=\"arm64\""));
    CHECK(has(xml, L"<Name>R&amp;D &lt;team&gt;</Name>"));
    CHECK(has(xml, L"<Group>Administrators</Group>"));
    CHECK(has(xml, L"<PlainText>true</PlainText>")); // empty password
    CHECK_FALSE(has(xml, L"AutoLogon"));
    CHECK_FALSE(has(xml, L"windowsPE"));
}

TEST_CASE("unattend: every option written, in its pass, and read back unchanged") {
    const UnattendOptions o = everything();
    const std::wstring xml = buildUnattendXml(o);

    const auto pe = xml.find(L"<settings pass=\"windowsPE\">");
    const auto specialize = xml.find(L"<settings pass=\"specialize\">");
    const auto oobe = xml.find(L"<settings pass=\"oobeSystem\">");
    REQUIRE(pe != std::wstring::npos);
    REQUIRE(specialize != std::wstring::npos);
    REQUIRE(oobe != std::wstring::npos);
    CHECK(pe < specialize);
    CHECK(specialize < oobe);
    auto in = [&](std::wstring_view fragment, std::size_t from, std::size_t to) {
        const auto at = xml.find(fragment);
        return at != std::wstring::npos && at > from && at < to;
    };
    CHECK(in(L"<SetupUILanguage>", pe, specialize));
    CHECK(in(L"<WillWipeDisk>true</WillWipeDisk>", pe, specialize));
    CHECK(in(L"<Type>EFI</Type>", pe, specialize));
    CHECK(in(L"<PartitionID>3</PartitionID>", pe, specialize));
    CHECK(in(L"<Key>/IMAGE/INDEX</Key>", pe, specialize));
    CHECK(in(L"<Key>W269N-WFGWX-YVC9B-4J6C9-T83GX</Key>", pe, specialize));
    CHECK(in(L"<AcceptEula>true</AcceptEula>", pe, specialize));
    // Every Windows 11 hardware check has its LabConfig value, in windowsPE (read before the checks run).
    for (const wchar_t* check : {L"BypassTPMCheck", L"BypassSecureBootCheck", L"BypassRAMCheck", L"BypassCPUCheck",
                                 L"BypassStorageCheck"}) {
        CAPTURE(check);
        CHECK(in(std::wstring(L"LabConfig /v ") + check + L" /t REG_DWORD /d 1 /f", pe, specialize));
    }
    CHECK(in(L"<ComputerName>WINLOVE-PC</ComputerName>", specialize, oobe));
    CHECK(in(L"<TimeZone>Turkey Standard Time</TimeZone>", specialize, oobe));
    CHECK(in(L"/v BypassNRO /t REG_DWORD /d 1 /f", specialize, oobe));
    CHECK(in(L"<HideOnlineAccountScreens>true</HideOnlineAccountScreens>", oobe, xml.size()));
    CHECK(in(L"<ProtectYourPC>3</ProtectYourPC>", oobe, xml.size()));
    CHECK(in(L"<LogonCount>1</LogonCount>", oobe, xml.size()));
    // The password is never written in clear text.
    CHECK_FALSE(has(xml, L"Parola123!"));
    CHECK(has(xml, L"<Value>UABhAHIAbwBsAGEAMQAyADMAIQBQAGEAcwBzAHcAbwByAGQA</Value>"));
    CHECK(has(xml, L"<PlainText>false</PlainText>"));

    const auto back = parseUnattendXml(utf8::fromWide(xml));
    REQUIRE(back);
    CHECK(*back == o);

    UnattendOptions mbr = o;
    mbr.disk = UnattendDisk::WipeMbr;
    const std::wstring legacy = buildUnattendXml(mbr);
    CHECK_FALSE(has(legacy, L"<Type>EFI</Type>"));
    CHECK(has(legacy, L"<Active>true</Active>"));
    CHECK(has(legacy, L"<PartitionID>2</PartitionID>\n          </InstallTo>"));
    const auto mbrBack = parseUnattendXml(utf8::fromWide(legacy));
    REQUIRE(mbrBack);
    CHECK(mbrBack->disk == UnattendDisk::WipeMbr);
}

TEST_CASE("unattend: password encoding is Setup's (UTF-16LE + element name, Base64)") {
    CHECK(encodeUnattendPassword(L"a") == L"YQBQAGEAcwBzAHcAbwByAGQA");
    CHECK(encodeUnattendPassword(L"şifre") == L"XwFpAGYAcgBlAFAAYQBzAHMAdwBvAHIAZAA=");
    CHECK(decodeUnattendPassword(L"XwFpAGYAcgBlAFAAYQBzAHMAdwBvAHIAZAA=") == L"şifre");
    CHECK(decodeUnattendPassword(encodeUnattendPassword(L"")) == L"");
    CHECK(decodeUnattendPassword(encodeUnattendPassword(L"x y&z")) == L"x y&z");
}

TEST_CASE("unattend: a file written by another tool is read for the options we know") {
    const auto o = parseUnattendXml(R"(<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend">
  <settings pass="windowsPE">
    <component name="Microsoft-Windows-International-Core-WinPE" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <SetupUILanguage><UILanguage>en-US</UILanguage></SetupUILanguage>
      <InputLocale>0409:00000409</InputLocale><UserLocale>en-GB</UserLocale>
    </component>
    <component name="Microsoft-Windows-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <RunSynchronous>
        <RunSynchronousCommand xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State" wcm:action="add">
          <Order>1</Order><Path>cmd /c reg.exe add "HKLM\SYSTEM\Setup\LabConfig" /v BypassSecureBootCheck /t REG_DWORD /d 1 /f</Path>
        </RunSynchronousCommand>
      </RunSynchronous>
      <UserData><ProductKey><Key>AAAAA-BBBBB-CCCCC-DDDDD-EEEEE</Key></ProductKey><AcceptEula>true</AcceptEula></UserData>
      <UseConfigurationSet>false</UseConfigurationSet>
    </component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <UserAccounts><LocalAccounts><LocalAccount><Name>Owner</Name><Group>Administrators</Group>
        <Password><Value>secret</Value><PlainText>true</PlainText></Password></LocalAccount></LocalAccounts></UserAccounts>
      <OOBE><ProtectYourPC>3</ProtectYourPC><HideEULAPage>true</HideEULAPage></OOBE>
      <FirstLogonCommands><SynchronousCommand><Order>1</Order><CommandLine>cmd /c echo hi</CommandLine></SynchronousCommand></FirstLogonCommands>
    </component>
  </settings>
</unattend>)");
    REQUIRE(o);
    CHECK(o->uiLanguage == L"en-US");
    CHECK(o->keyboard == L"0409:00000409");
    CHECK(o->locale == L"en-GB");
    CHECK(o->bypassSecureBoot);
    CHECK_FALSE(o->bypassTpm);
    CHECK(o->productKey == L"AAAAA-BBBBB-CCCCC-DDDDD-EEEEE");
    CHECK(o->acceptEula);
    CHECK(o->accountName == L"Owner");
    CHECK(o->password == L"secret");
    CHECK(o->skipPrivacy);
    CHECK_FALSE(o->skipOnlineAccount);
    CHECK(o->disk == UnattendDisk::Ask);

    CHECK_FALSE(parseUnattendXml("<notunattend/>"));
    CHECK_FALSE(parseUnattendXml("<unattend><settings>"));
    CHECK_FALSE(parseUnattendXml(""));
}

TEST_CASE("unattend: validation") {
    CHECK(validateUnattend({}).empty());
    CHECK(validateUnattend(everything()).empty());

    UnattendOptions o;
    o.computerName = L"THIS-NAME-IS-TOO-LONG";
    o.accountName = L"Administrator";
    o.productKey = L"12345";
    CHECK(validateUnattend(o) == std::vector{UnattendProblem::ComputerName, UnattendProblem::AccountName,
                                             UnattendProblem::ProductKey});
    o = {};
    o.computerName = L"MY PC";
    CHECK(validateUnattend(o) == std::vector{UnattendProblem::ComputerName});
    o.computerName = L"12345";
    CHECK(validateUnattend(o) == std::vector{UnattendProblem::ComputerName});
    o.computerName = L"LAB_PC-01";
    CHECK(validateUnattend(o).empty());
    o.accountName = L"a/b";
    CHECK(validateUnattend(o) == std::vector{UnattendProblem::AccountName});
    o.accountName.clear();
    o.autoLogon = true;
    CHECK(validateUnattend(o) == std::vector{UnattendProblem::AutoLogonNeedsAccount});
}

TEST_CASE("unattend: <UserData> never goes out without a <ProductKey>") {
    // What a user's first VM run hit: EULA accepted, no key → Setup stopped with "cannot read the
    // <ProductKey> setting from the unattend answer file".
    UnattendOptions o;
    o.acceptEula = true;
    o.editionId = L"CoreSingleLanguage"; // the image has this one edition
    std::wstring xml = buildUnattendXml(o);
    CHECK(has(xml, L"<UserData>\n        <ProductKey>\n          <Key>BT79Q-G7N6G-PGBYW-4YWX6-6F4BT</Key>\n"
                   L"          <WillShowUI>OnError</WillShowUI>\n        </ProductKey>\n        <AcceptEula>true</AcceptEula>"));

    // Several editions and none picked: Setup is to ask, which takes the placeholder key.
    o.editionId.clear();
    xml = buildUnattendXml(o);
    CHECK(has(xml, L"<Key>00000-00000-00000-00000-00000</Key>"));
    CHECK(has(xml, L"<WillShowUI>Always</WillShowUI>"));

    // An edition without a generic key here is asked for as well.
    o.editionId = L"IoTEnterprise";
    CHECK(has(buildUnattendXml(o), L"<WillShowUI>Always</WillShowUI>"));

    // The user's own key wins over the edition's.
    o.editionId = L"Professional";
    o.productKey = L"AAAAA-BBBBB-CCCCC-DDDDD-EEEEE";
    xml = buildUnattendXml(o);
    CHECK(has(xml, L"<Key>AAAAA-BBBBB-CCCCC-DDDDD-EEEEE</Key>"));
    CHECK_FALSE(has(xml, L"VK7JG"));
    CHECK(has(xml, L"<WillShowUI>OnError</WillShowUI>"));

    // Nothing asked of <UserData>: it is not written at all.
    UnattendOptions bare;
    bare.editionId = L"Professional";
    bare.bypassTpm = true;
    CHECK_FALSE(has(buildUnattendXml(bare), L"UserData"));
}

TEST_CASE("unattend: a generic key or the placeholder read from a file is not the user's key") {
    UnattendOptions o;
    o.acceptEula = true;
    o.editionId = L"Professional";
    auto back = parseUnattendXml(utf8::fromWide(buildUnattendXml(o)));
    REQUIRE(back);
    CHECK(back->productKey.empty()); // else a preset made on Pro would force Pro's key onto a Home image
    CHECK(back->acceptEula);

    o.editionId.clear();
    back = parseUnattendXml(utf8::fromWide(buildUnattendXml(o)));
    REQUIRE(back);
    CHECK(back->productKey.empty());

    CHECK(genericProductKey(L"Core") == L"YTMG3-N6DKC-DKB77-7M9GH-8HVX7");
    CHECK(genericProductKey(L"Professional") == L"VK7JG-NPHTM-C97JM-9MPGT-3V66T");
    CHECK(genericProductKey(L"Nope").empty());
}

TEST_CASE("welcome (D-085): no account, the welcome last in specialize, OOBE's pages hidden; read back as the welcome") {
    UnattendOptions o;
    o.accountName = L"Berkay"; // kept for when the welcome is switched off again
    o.password = L"secret";
    o.autoLogon = true;
    o.specializeCommands = {L"cmd /c echo mine"};
    o.firstLogonCommands = {L"cmd /c echo later"};
    o.computerName = L"OFIS-PC";             // the welcome's first answers, not the file's
    o.timeZone = L"Turkey Standard Time";
    o.welcome = true;
    const std::string xml = utf8::fromWide(buildUnattendXml(o));
    CHECK(xml.find("<ComputerName>OFIS-PC</ComputerName>") != std::string::npos); // set before the welcome renames
    CHECK(xml.find("<TimeZone>") == std::string::npos);
    CHECK(xml.find("<Name>Berkay</Name>") == std::string::npos);
    CHECK(xml.find("WinLoveSetup") == std::string::npos);
    CHECK(xml.find("<AutoLogon>") == std::string::npos);
    CHECK(xml.find("<HideOnlineAccountScreens>true</HideOnlineAccountScreens>") != std::string::npos);
    CHECK(xml.find("<HideLocalAccountScreen>true</HideLocalAccountScreen>") != std::string::npos);
    CHECK(xml.find("<ProtectYourPC>3</ProtectYourPC>") != std::string::npos);
    CHECK(xml.find("<HideEULAPage>true</HideEULAPage>") != std::string::npos);
    const auto specializeAt = xml.find("<settings pass=\"specialize\">");
    const auto oobeAt = xml.find("<settings pass=\"oobeSystem\">");
    const auto welcomeAt = xml.find("oobe.ps1");
    const auto mineAt = xml.find("echo mine");
    REQUIRE(welcomeAt != std::string::npos);
    CHECK(specializeAt < welcomeAt);
    CHECK(welcomeAt < oobeAt);
    CHECK(mineAt < welcomeAt); // the welcome is the last specialize command
    CHECK(xml.find("start \"\" powershell") == std::string::npos); // Setup waits for it
    CHECK(validateUnattend(o).empty());

    const auto back = parseUnattendXml(xml);
    REQUIRE(back);
    CHECK(back->welcome);
    CHECK_FALSE(back->hideLocalAccount);
    CHECK(back->accountName.empty());
    CHECK_FALSE(back->autoLogon);
    CHECK(back->specializeCommands == std::vector<std::wstring>{L"cmd /c echo mine"});
    CHECK(back->firstLogonCommands == std::vector<std::wstring>{L"cmd /c echo later"});
    // No name given (or a random one): the welcome's placeholder, so OOBE leaves the welcome's name.
    o.computerName.clear();
    o.randomComputerName = true;
    CHECK(utf8::fromWide(buildUnattendXml(o)).find("<ComputerName>WINLOVE-PC</ComputerName>") != std::string::npos);
    o.randomComputerName = false;
    o.computerName = L"OFIS-PC";

    // Off: the account written as it was.
    o.welcome = false;
    const std::string plain = utf8::fromWide(buildUnattendXml(o));
    CHECK(plain.find("<Name>Berkay</Name>") != std::string::npos);
    CHECK(plain.find("oobe.ps1") == std::string::npos);
    CHECK(plain.find("HideLocalAccountScreen") == std::string::npos);
    CHECK(plain.find("<ComputerName>OFIS-PC</ComputerName>") != std::string::npos);
}

TEST_CASE("welcome: D-084's answer files (a setup account starting it) read back as the welcome") {
    UnattendOptions o;
    o.accountName = L"WinLoveSetup";
    o.password = L"Xy7pQ2mN8rT4vW6zK3bC";
    o.autoLogon = true;
    o.firstLogonCommands = {LR"(cmd /c start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%ProgramData%\WinLove\Oobe\oobe.ps1")",
                            L"cmd /c echo mine"};
    const auto back = parseUnattendXml(utf8::fromWide(buildUnattendXml(o)));
    REQUIRE(back);
    CHECK(back->welcome);
    CHECK(back->accountName.empty());
    CHECK_FALSE(back->autoLogon);
    CHECK(back->firstLogonCommands == std::vector<std::wstring>{L"cmd /c echo mine"});
    CHECK(utf8::fromWide(buildUnattendXml(*back)).find("WinLoveSetup") == std::string::npos);
}

TEST_CASE("welcome: oobe.json carries the pages, the defaults and the texts; the plan reads back from the queue") {
    WelcomePlan plan;
    plan.computerPage = false;
    plan.theme = L"light";
    plan.accent = L"#107C10";
    plan.privacy = L"windows";
    plan.computerName = L"OFIS-PC";
    plan.timeZone = L"Turkey Standard Time";
    plan.prefsPage = true;
    plan.texts = {{"accountHeading", L"Hoş geldin"}, {"themeDark", L"Koyu"}, {"accentGreen", L"Yeşil"}, {"privacyStrict", L"Az veri"}};
    const std::string json = welcomeJson(plan);
    CHECK(json.find("WinLoveSetup") == std::string::npos);
    CHECK(json.find("\"computer\"") == std::string::npos); // not among the pages
    CHECK(json.find("\"network\"") != std::string::npos);
    CHECK(json.find("\"prefs\"") != std::string::npos);
    CHECK(json.find("HideFileExt") != std::string::npos);      // the habits and what they write
    CHECK(json.find("AllowNewsAndInterests") != std::string::npos);
    CHECK(json.find("\"windows11\": true") != std::string::npos); // the classic menu: Windows 11 only
    CHECK(json.find("\"OFIS-PC\"") != std::string::npos);
    CHECK(json.find("AllowTelemetry") != std::string::npos);
    CHECK(json.find(utf8::fromWide(L"Yeşil")) != std::string::npos);

    const auto ops = welcomeOperations(plan);
    REQUIRE(ops.size() == 2);
    CHECK(ops[0].target == LR"(ProgramData\WinLove\Oobe\oobe.ps1)");
    CHECK(ops[0].value.find(L"WinLove: its own setup screens") != std::wstring::npos);
    CHECK(ops[0].value.find(L"@@dark.") == std::wstring::npos); // the colours were filled in at build time
    CHECK(welcomeSlots().size() == 2);
    const auto back = welcomePlanFromOperations(ops);
    REQUIRE(back);
    std::ranges::sort(plan.texts); // oobe.json keeps them by key
    CHECK(*back == plan);
    CHECK_FALSE(welcomePlanFromOperations({}).has_value());

    CHECK(json.find("\"Turkey Standard Time\"") != std::string::npos);
    CHECK(welcomeSetupCommand().find(L"oobe.ps1") != std::wstring::npos);
}

TEST_CASE("welcome (D-103): the texts of every language ride in oobe.json, keyed for runtime lookup") {
    WelcomePlan plan;
    plan.texts = {{"privacyStrict", L"Az veri"}, {"themeDark", L"Koyu"}};
    std::vector<WelcomeLanguage> languages{
        {"en", {{"privacyStrict", L"Little data"}, {"themeDark", L"Dark"}}},
        {"de", {{"privacyStrict", L"Wenig Daten"}, {"themeDark", L"Dunkel"}}},
    };
    const std::string json = welcomeJson(plan, languages);
    // Structural items now carry the key so oobe.ps1 can re-read the name in the chosen language.
    CHECK(json.find("\"nameKey\": \"privacyStrict\"") != std::string::npos);
    CHECK(json.find("\"nameKey\": \"themeDark\"") != std::string::npos);
    // The per-language map is present with both languages' values.
    CHECK(json.find("\"textsByLang\"") != std::string::npos);
    CHECK(json.find("Wenig Daten") != std::string::npos);
    CHECK(json.find("Little data") != std::string::npos);
    // No languages -> no textsByLang (back-compat for preview and old scripts).
    CHECK(welcomeJson(plan).find("textsByLang") == std::string::npos);
    // The embedded welcome-langs.json parses back to the same languages.
    const auto parsed = welcomeLanguagesFromJson(
        R"({"en":{"themeDark":"Dark","privacyStrict":"Little data"},"de":{"themeDark":"Dunkel","privacyStrict":"Wenig Daten"}})");
    REQUIRE(parsed.size() == 2);
    const auto en = std::ranges::find_if(parsed, [](const WelcomeLanguage& l) { return l.code == "en"; });
    REQUIRE(en != parsed.end());
    CHECK(en->texts.size() == 2);
    CHECK(welcomeLanguagesFromJson("not json").empty());
}

TEST_CASE("welcome (D-087): a preset's old script gives way to the app's own; its choices stay") {
    WelcomePlan plan;
    plan.theme = L"light";
    ops::ChangeSet saved;
    saved.addAll(welcomeOperations(plan));
    saved.add(ops::Operation{ops::OpKind::WriteFile, LR"(ProgramData\WinLove\Oobe\oobe.ps1)", L"# an older oobe.ps1"});
    saved.add(ops::Operation{ops::OpKind::SetServiceStart, L"SysMain", L"disabled"});
    const auto now = withCurrentWelcomeScript(saved);
    REQUIRE(now.size() == 3);
    const auto* script = now.find(ops::OpKind::WriteFile, LR"(ProgramData\WinLove\Oobe\oobe.ps1)");
    REQUIRE(script);
    CHECK(script->value == welcomeOperations(plan)[0].value);
    CHECK(script->value.find(L"PreserveWhitespace") != std::wstring::npos); // the empty password stays empty
    REQUIRE(welcomePlanFromOperations(now.operations()));
    CHECK(welcomePlanFromOperations(now.operations())->theme == L"light");
    CHECK(now.find(ops::OpKind::SetServiceStart, L"SysMain"));
    // Without the welcome nothing is added.
    ops::ChangeSet other;
    other.add(ops::Operation{ops::OpKind::SetServiceStart, L"SysMain", L"disabled"});
    CHECK(withCurrentWelcomeScript(other).size() == 1);
}

TEST_CASE("unattend: the automatic sign-in's password does not stay in the registry (audit B2)") {
    UnattendOptions o;
    o.accountName = L"berkay";
    o.password = L"secret";
    o.autoLogon = true;
    o.firstLogonCommands = {L"cmd /c echo mine"};
    const std::wstring xml = buildUnattendXml(o);
    const auto mine = xml.find(L"cmd /c echo mine");
    const auto cleanup = xml.find(L"/v DefaultPassword /f");
    REQUIRE(mine != std::wstring::npos);
    REQUIRE(cleanup != std::wstring::npos);
    CHECK(cleanup > mine); // after the user's own commands
    CHECK(xml.find(LR"(HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon)") != std::wstring::npos);

    const auto back = parseUnattendXml(utf8::fromWide(xml));
    REQUIRE(back);
    CHECK(back->firstLogonCommands == std::vector<std::wstring>{L"cmd /c echo mine"}); // not a command of the user's
    CHECK(buildUnattendXml(*back) == xml); // saved and read again: one cleanup, not two

    o.autoLogon = false;
    CHECK(buildUnattendXml(o).find(L"DefaultPassword") == std::wstring::npos);
}
