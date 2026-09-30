// P13: answer file generation, reading it back, validation, Setup's password encoding.
#include "base/Utf8.h"
#include "core/unattend/Unattend.h"

#include <doctest.h>

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
    o.productKey = L"VK7JG-NPHTM-C97JM-9MPGT-3V66T";
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
    CHECK(in(L"<Key>VK7JG-NPHTM-C97JM-9MPGT-3V66T</Key>", pe, specialize));
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
