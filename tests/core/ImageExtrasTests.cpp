// 2026-10-01 engine pieces (D-048 … D-054) that need no image mounted: scheduled tasks, hosts
// sections, files into the image, international settings, default apps, language files, app
// provisioning arguments, update ordering.
#include "core/image/AppxInstall.h"
#include "core/image/HostsFile.h"
#include "core/image/ImageFiles.h"
#include "core/image/LanguagePacks.h"
#include "core/image/ScheduledTasks.h"
#include "core/image/UpdatePackage.h"
#include "core/image/dism/DefaultApps.h"
#include "core/image/dism/Intl.h"
#include "core/ops/Planner.h"
#include "core/postsetup/SetupScripts.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace wl;
using namespace wl::core;

namespace {

std::filesystem::path image(const wchar_t* name) {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"image-extras" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / L"Windows" / L"System32" / L"drivers" / L"etc");
    std::filesystem::create_directories(dir / L"Users" / L"Public");
    return dir;
}

std::string bytesOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

} // namespace

TEST_CASE("tasks: paths, the script in the image, and taking a task back") {
    CHECK(validTaskPath(L"\\Microsoft\\Windows\\Application Experience\\Microsoft Compatibility Appraiser"));
    CHECK(validTaskPath(L"\\XblGameSaveTask"));
    CHECK_FALSE(validTaskPath(L"Microsoft\\Windows\\Autochk\\Proxy")); // no leading backslash
    CHECK_FALSE(validTaskPath(L"\\Microsoft\\..\\Evil"));
    CHECK_FALSE(validTaskPath(L"\\a\" & del *"));
    CHECK_FALSE(validTaskPath(L"\\a\\\\b"));
    CHECK_FALSE(validTaskPath(L"\\a\\"));
    CHECK_FALSE(validTaskPath(L"\\%TEMP%"));

    const auto dir = image(L"tasks");
    const std::wstring appraiser = L"\\Microsoft\\Windows\\Application Experience\\Microsoft Compatibility Appraiser";
    const std::wstring proxy = L"\\Microsoft\\Windows\\Autochk\\Proxy";
    REQUIRE(setTaskDisabled(dir, appraiser, true));
    REQUIRE(setTaskDisabled(dir, proxy, true));
    REQUIRE(setTaskDisabled(dir, L"\\MICROSOFT\\Windows\\Autochk\\Proxy", true)); // case-insensitive: once
    auto tasks = readDisabledTasks(dir);
    REQUIRE(tasks.size() == 2);
    CHECK(tasks[0] == appraiser);
    const std::string script = bytesOf(setupScriptPath(dir, kTasksScript));
    CHECK(script.starts_with("@echo off\r\nchcp 65001 >nul\r\n"));
    CHECK(script.find("schtasks /Change /TN \"\\Microsoft\\Windows\\Autochk\\Proxy\" /Disable") != std::string::npos);
    const std::string complete = bytesOf(dir / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd");
    CHECK(complete.find("if exist \"%SystemRoot%\\Setup\\Scripts\\WinLove\\tasks.cmd\" call") != std::string::npos);

    REQUIRE(setTaskDisabled(dir, appraiser, false));
    CHECK(readDisabledTasks(dir) == std::vector<std::wstring>{proxy});
    REQUIRE(setTaskDisabled(dir, proxy, false));
    CHECK_FALSE(std::filesystem::exists(setupScriptPath(dir, kTasksScript))); // the last one out deletes it
    CHECK_FALSE(setTaskDisabled(dir, L"no path", true));
}

TEST_CASE("hosts: parsing, sections kept apart, the rest of the file untouched") {
    const auto entries = parseHosts(L"# comment\r\n127.0.0.1 localhost\r\n0.0.0.0 Vortex.Data.Microsoft.com  a.example.com # x\r\n"
                                    L"bad line\r\n999.1.1.1 no.example\r\n::1 six.example.com\r\n0.0.0.0 a.example.com\r\n");
    REQUIRE(entries.size() == 3);
    CHECK(entries[0] == HostEntry{L"0.0.0.0", L"vortex.data.microsoft.com"});
    CHECK(entries[1].name == L"a.example.com");
    CHECK(entries[2] == HostEntry{L"::1", L"six.example.com"});

    const std::wstring original = L"# Copyright (c) 1993-2009 Microsoft Corp.\r\n#\r\n# 127.0.0.1 localhost\r\n";
    std::wstring file = setHostsSection(original, L"telemetry", L"0.0.0.0 a.example.com\r\n");
    file = setHostsSection(file, L"custom", L"0.0.0.0 b.example.com\r\n0.0.0.0 c.example.com\r\n");
    CHECK(file.starts_with(original));
    auto sections = hostsSections(file);
    REQUIRE(sections.size() == 2);
    CHECK(sections[L"telemetry"] == L"0.0.0.0 a.example.com\r\n");
    // Replacing one section keeps its place and the other one.
    file = setHostsSection(file, L"telemetry", L"0.0.0.0 z.example.com\r\n");
    sections = hostsSections(file);
    CHECK(sections[L"telemetry"] == L"0.0.0.0 z.example.com\r\n");
    CHECK(file.find(L"telemetry") < file.find(L"custom"));
    file = setHostsSection(file, L"telemetry", L"");
    file = setHostsSection(file, L"custom", L"");
    CHECK(file == original);

    const auto dir = image(L"hosts");
    write(hostsPath(dir), "# Microsoft\r\n");
    REQUIRE(applyHostsSection(dir, L"telemetry", L"0.0.0.0 a.example.com\r\nnot a line\r\n"));
    CHECK(bytesOf(hostsPath(dir)) == "# Microsoft\r\n# >>> WinLove: telemetry\r\n0.0.0.0 a.example.com\r\n# <<< WinLove: telemetry\r\n");
    CHECK(readHostsSections(dir).size() == 1);
    CHECK_FALSE(applyHostsSection(dir, L"Bad Id", L""));
}

TEST_CASE("files into the image: allowed places, copied whole") {
    CHECK(validateTreeTarget(L"Tools\\Sysinternals"));
    CHECK(validateTreeTarget(L"Users\\Public\\Desktop\\readme.txt"));
    CHECK(validateTreeTarget(L"Windows\\Web\\Wallpaper\\Ours"));
    CHECK(treeTargetRisky(L"Windows\\Web\\Wallpaper\\Ours"));
    CHECK_FALSE(treeTargetRisky(L"Tools"));
    CHECK_FALSE(validateTreeTarget(L""));
    CHECK_FALSE(validateTreeTarget(L"..\\outside"));
    CHECK_FALSE(validateTreeTarget(L"C:\\Tools"));
    CHECK_FALSE(validateTreeTarget(L"Windows\\System32\\config\\SYSTEM"));
    CHECK_FALSE(validateTreeTarget(L"windows\\winsxs\\x"));
    CHECK_FALSE(validateTreeTarget(L"Windows")); // a parent of what Windows owns: its whole content would be at stake
    CHECK_FALSE(validateTreeTarget(L"Program Files\\WindowsApps\\x"));

    const auto dir = image(L"tree");
    const auto source = std::filesystem::temp_directory_path() / L"wl-tests" / L"image-extras" / L"tree-source";
    std::filesystem::remove_all(source);
    write(source / L"a.txt", "A");
    write(source / L"sub" / L"b.txt", "BB");
    auto copied = copyImageTree(dir, L"Tools\\Mine", source, TaskContext{});
    REQUIRE(copied);
    CHECK(*copied == 3);
    CHECK(bytesOf(dir / L"Tools" / L"Mine" / L"sub" / L"b.txt") == "BB");
    // A single file, onto a file already there.
    write(dir / L"Users" / L"Public" / L"note.txt", "old");
    REQUIRE(copyImageTree(dir, L"Users\\Public\\note.txt", source / L"a.txt", TaskContext{}));
    CHECK(bytesOf(dir / L"Users" / L"Public" / L"note.txt") == "A");
    CHECK(treeSize(source) == 3);
    CHECK_FALSE(copyImageTree(dir, L"Tools\\X", source / L"missing", TaskContext{}));
}

TEST_CASE("intl: dism.exe output, arguments, validation") {
    const std::string output =
        "Deployment Image Servicing and Management tool\r\nVersion: 10.0.26100.1\r\n\r\nImage Version: 10.0.26200.8037\r\n\r\n"
        "Reporting offline international settings.\r\n\r\n"
        "Default system UI language : tr-TR\r\nSystem locale : tr-TR\r\nDefault time zone : Turkey Standard Time\r\n"
        "User locale for default user : tr-TR\r\nLocation : Turkey\r\nActive keyboard(s) : 041f:0000041f, 0409:00000409\r\n"
        "Keyboard layered driver : PC/AT Enhanced Keyboard (101/102-Key)\r\n\r\n"
        "Installed language(s): tr-TR\r\n      Type : Fully localized language.\r\n"
        "Installed language(s): en-US\r\n      Type : Partially localized language, MUI type.\r\n"
        "\r\nThe operation completed successfully.\r\n";
    const auto intl = parseIntl(output);
    CHECK(intl.current.uiLanguage == L"tr-TR");
    CHECK(intl.current.timeZone == L"Turkey Standard Time");
    CHECK(intl.current.inputLocale == L"041f:0000041f");
    CHECK(intl.languages == std::vector<std::wstring>{L"tr-TR", L"en-US"});

    IntlSettings s;
    s.uiLanguage = L"en-US";
    s.timeZone = L"GMT Standard Time";
    CHECK(intlArguments(s) == L"/Set-UILang:en-US /Set-TimeZone:\"GMT Standard Time\"");
    CHECK(intlArguments(IntlSettings{}).empty());
    auto back = intlFromJson(intlToJson(s));
    REQUIRE(back);
    CHECK(*back == s);

    CHECK(isLanguageTag(L"sr-Latn-RS"));
    CHECK_FALSE(isLanguageTag(L"t"));
    CHECK_FALSE(isLanguageTag(L"tr_TR"));
    CHECK(isInputLocale(L"041f:0000041f"));
    CHECK(isInputLocale(L"tr-TR"));
    CHECK_FALSE(isInputLocale(L"041f:41f"));
    CHECK_FALSE(isTimeZoneId(L"Turkey\" /Set-SysLocale:x"));
    s.timeZone = L"Bad\"Zone";
    CHECK_FALSE(validateIntl(s));
    CHECK_FALSE(intlFromJson(R"({"ui":"not a tag"})"));
}

TEST_CASE("default apps: the XML read and written back") {
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<DefaultAssociations>
  <Association Identifier=".pdf" ProgId="AcroExch.Document.DC" ApplicationName="Adobe Acrobat" />
  <Association Identifier="http" ProgId="ChromeHTML" ApplicationName="Google Chrome" />
  <Association Identifier=".pdf" ProgId="MSEdgePDF" ApplicationName="Microsoft Edge" />
  <Association Identifier="" ProgId="x" />
</DefaultAssociations>)";
    auto list = parseAssociations(xml);
    REQUIRE(list);
    REQUIRE(list->size() == 2);
    CHECK((*list)[0].identifier == L"http");
    CHECK((*list)[1].progId == L"MSEdgePDF"); // the last one of an identifier wins
    auto again = parseAssociations(associationsXml(*list));
    REQUIRE(again);
    CHECK(*again == *list);
    CHECK_FALSE(parseAssociations("<Other/>"));
    CHECK_FALSE(parseAssociations("not xml"));
}

TEST_CASE("language files: Microsoft's names") {
    auto lp = classifyLanguageFile(LR"(D:\LanguagesAndOptionalFeatures\Microsoft-Windows-Client-Language-Pack_x64_tr-tr.cab)");
    CHECK(lp.kind == LanguagePackFile::Kind::LanguagePack);
    CHECK(lp.language == L"tr-TR");
    CHECK(lp.architecture == L"x64");
    auto basic = classifyLanguageFile(LR"(D:\Microsoft-Windows-LanguageFeatures-Basic-sr-latn-rs-Package~31bf3856ad364e35~amd64~~.cab)");
    CHECK(basic.kind == LanguagePackFile::Kind::Basic);
    CHECK(basic.language == L"sr-Latn-RS");
    CHECK(basic.architecture == L"x64");
    auto tts = classifyLanguageFile(LR"(D:\Microsoft-Windows-LanguageFeatures-TextToSpeech-en-us-Package~31bf3856ad364e35~arm64~~.cab)");
    CHECK(tts.kind == LanguagePackFile::Kind::TextToSpeech);
    CHECK(tts.architecture == L"arm64");
    auto fonts = classifyLanguageFile(LR"(D:\Microsoft-Windows-LanguageFeatures-Fonts-Jpan-Package~31bf3856ad364e35~amd64~~.cab)");
    CHECK(fonts.kind == LanguagePackFile::Kind::Fonts);
    CHECK(fonts.language.empty());
    CHECK_FALSE(isLanguageFile(LR"(D:\windows11.0-kb5043080-x64.msu)"));
    CHECK_FALSE(isLanguageFile(LR"(D:\Microsoft-Windows-Client-Language-Pack_x64_tr-tr.msu)"));
    CHECK(canonicalLanguageTag(L"zh-hans-cn") == L"zh-Hans-CN");
    // As an update package: kind "language", ordered after the servicing stack, before the LCU.
    CHECK(analyzeUpdate(lp.path).kind == UpdateKind::Language);
    ops::ChangeSet changes;
    changes.add({ops::OpKind::AddPackage, L"C:\\u\\lcu.msu", L"lcu"});
    changes.add({ops::OpKind::AddPackage, L"C:\\u\\lp.cab", L"language"});
    changes.add({ops::OpKind::AddPackage, L"C:\\u\\ssu.msu", L"ssu"});
    const auto plan = ops::plan(changes);
    REQUIRE(plan.steps.size() == 3);
    CHECK(plan.steps[0].operation.value == L"ssu");
    CHECK(plan.steps[1].operation.value == L"language");
    CHECK(plan.steps[2].operation.value == L"lcu");
}

TEST_CASE("apps: dism.exe arguments and the queued details") {
    AppxInstall install;
    install.package = LR"(C:\Apps\Terminal.msixbundle)";
    install.dependencies = {LR"(C:\Apps\Dependencies\x64\Microsoft.UI.Xaml.2.8_8.2310.30001.0_x64.appx)"};
    CHECK(appxArguments(install) ==
          L"/Add-ProvisionedAppxPackage /PackagePath:\"C:\\Apps\\Terminal.msixbundle\" "
          L"/DependencyPackagePath:\"C:\\Apps\\Dependencies\\x64\\Microsoft.UI.Xaml.2.8_8.2310.30001.0_x64.appx\" "
          L"/SkipLicense /Region:all");
    install.license = LR"(C:\Apps\Terminal_License1.xml)";
    install.missing = {L"Microsoft.VCLibs.140.00.UWPDesktop"};
    CHECK(appxArguments(install).find(L"/LicensePath:\"C:\\Apps\\Terminal_License1.xml\"") != std::wstring::npos);
    auto back = appxInstallFromJson(install.package, appxInstallToJson(install));
    REQUIRE(back);
    CHECK(*back == install);
    CHECK(isAppxFile(L"x.MSIXBUNDLE"));
    CHECK_FALSE(isAppxFile(L"x.zip"));
    CHECK_FALSE(readAppxPackage(L"C:\\nope.appx"));
    // New operations: their phases.
    CHECK(ops::phaseOf(ops::OpKind::AddAppx) == ops::Phase::Apps);
    CHECK(ops::phaseOf(ops::OpKind::RemoveDriver) == ops::Phase::Remove);
    CHECK(ops::phaseOf(ops::OpKind::SetIntl) == ops::Phase::Settings);
    CHECK(ops::opKindFromKey("setTaskState").value() == ops::OpKind::SetTaskState);
}
