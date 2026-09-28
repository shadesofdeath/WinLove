// P11: .reg syntax, key normalization / offline hive mapping, ChangeSet round trip.
#include "core/image/RegistryEdit.h"
#include "core/image/Services.h"

#include <doctest.h>

#include <windows.h>

#include <fstream>

using namespace wl;
using namespace wl::core;

TEST_CASE("normalizeRegistryKey accepts long and short roots") {
    CHECK(normalizeRegistryKey(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\X\\") == L"HKLM\\SOFTWARE\\X");
    CHECK(normalizeRegistryKey(L"hkcu\\Software") == L"HKCU\\Software");
    CHECK(normalizeRegistryKey(L"HKEY_CURRENT_CONFIG\\X").empty());
    CHECK(normalizeRegistryKey(L"HKLMX\\Y").empty()); // whole segment only
    CHECK(isUserKey(L"HKEY_CURRENT_USER\\Control Panel"));
    CHECK_FALSE(isUserKey(L"HKLM\\SOFTWARE"));
}

TEST_CASE("mapOfflineKey routes to the image's hive files") {
    auto k = mapOfflineKey(L"HKLM\\SOFTWARE\\Policies\\Microsoft");
    REQUIRE(k);
    CHECK(k->hive == OfflineHiveFile::Software);
    CHECK(k->path == L"Policies\\Microsoft");
    CHECK(mapOfflineKey(L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\X")->hive == OfflineHiveFile::System);
    CHECK(mapOfflineKey(L"HKCR\\*\\shell")->path == L"Classes\\*\\shell");
    CHECK(mapOfflineKey(L"HKCU\\Software\\Classes\\CLSID\\{x}")->hive == OfflineHiveFile::DefaultUserClasses);
    CHECK(mapOfflineKey(L"HKCU\\Software\\Classes\\CLSID\\{x}")->path == L"CLSID\\{x}");
    CHECK(mapOfflineKey(L"HKCU\\Software\\Microsoft")->hive == OfflineHiveFile::DefaultUser);
    CHECK(mapOfflineKey(L"HKU\\.DEFAULT\\Control Panel")->hive == OfflineHiveFile::DotDefault);
    CHECK_FALSE(mapOfflineKey(L"HKLM\\SAM\\SAM"));
    CHECK_FALSE(mapOfflineKey(L"HKU\\S-1-5-21-1\\Software"));
    CHECK(hiveFilePath(L"C:\\m", OfflineHiveFile::DefaultUser) == std::filesystem::path(L"C:\\m\\Users\\Default\\NTUSER.DAT"));
}

TEST_CASE("parseRegText: strings, dword, hex types, continuation, deletes") {
    const auto writes = parseRegText(LR"(Windows Registry Editor Version 5.00

; comment
[HKEY_CURRENT_USER\Software\Test]
"Name"="Value with \"quotes\" and \\ slash"
@="default"
"Count"=dword:0000002a
"Bin"=hex:01,02,\
  03,ff
"Path"=hex(2):25,00,00,00
"Gone"=-

[-HKEY_LOCAL_MACHINE\SOFTWARE\Old]
)");
    REQUIRE(writes);
    REQUIRE(writes->size() == 7);
    const auto& w = *writes;
    CHECK(w[0].key == L"HKCU\\Software\\Test");
    CHECK(w[0].name == L"Name");
    CHECK(formatRegValue(w[0]) == LR"("Value with \"quotes\" and \\ slash")");
    CHECK(w[1].name.empty());
    CHECK(w[2].type == REG_DWORD);
    CHECK(formatRegValue(w[2]) == L"dword:0000002a");
    CHECK(w[3].type == REG_BINARY);
    CHECK(w[3].data == std::vector<std::uint8_t>{1, 2, 3, 0xff});
    CHECK(w[4].type == REG_EXPAND_SZ);
    CHECK(formatRegValue(w[4]) == L"hex(2):25,00,00,00");
    CHECK(w[5].kind == RegistryWrite::Kind::DeleteValue);
    CHECK(w[6].kind == RegistryWrite::Kind::DeleteKey);
    CHECK(w[6].key == L"HKLM\\SOFTWARE\\Old");
}

TEST_CASE("parseRegText reports bad input with a line number") {
    CHECK_FALSE(parseRegText(L"[HKLM\\X]\n\"a\"=\"b\"\n")); // no header
    auto bad = parseRegText(L"REGEDIT4\n[HKLM\\SOFTWARE\\X]\n\"a\"=dword:xyz\n");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().context == L".reg line 3");
    CHECK_FALSE(parseRegText(L"REGEDIT4\n\"a\"=\"b\"\n")); // value before any key
}

TEST_CASE("operation target/value round-trip") {
    auto w = parseRegValue(L"HKEY_CURRENT_USER\\Control Panel\\Desktop", L"MenuShowDelay", L"\"100\"");
    REQUIRE(w);
    const auto back = registryWriteFrom(registryTarget(*w), formatRegValue(*w));
    REQUIRE(back);
    CHECK(*back == *w);
    auto del = registryWriteFrom(L"HKLM\\SOFTWARE\\X::", L"[-]");
    REQUIRE(del);
    CHECK(del->kind == RegistryWrite::Kind::DeleteKey);
    CHECK_FALSE(registryWriteFrom(L"HKLM\\SOFTWARE\\X", L"\"a\""));
}

TEST_CASE("serviceStartWrites: Start and DelayedAutostart under CurrentControlSet") {
    const auto writes = serviceStartWrites(L"DiagTrack", StartType::AutoDelayed);
    REQUIRE(writes.size() == 2);
    CHECK(registryTarget(writes[0]) == L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\DiagTrack::Start");
    CHECK(formatRegValue(writes[0]) == L"dword:00000002");
    CHECK(formatRegValue(writes[1]) == L"dword:00000001");
    CHECK(formatRegValue(serviceStartWrites(L"X", StartType::Disabled)[0]) == L"dword:00000004");
}

TEST_CASE("formatRegText writes long roots and parses back to the same writes") {
    auto writes = parseRegText(L"REGEDIT4\n[HKCU\\Software\\A]\n\"x\"=dword:00000001\n@=\"d\"\n[-HKLM\\SOFTWARE\\Old]\n");
    REQUIRE(writes);
    const std::wstring text = formatRegText(*writes);
    CHECK(text.find(L"[HKEY_CURRENT_USER\\Software\\A]") != std::wstring::npos);
    CHECK(text.find(L"[-HKEY_LOCAL_MACHINE\\SOFTWARE\\Old]") != std::wstring::npos);
    auto back = parseRegText(text);
    REQUIRE(back);
    CHECK(*back == *writes);
}

TEST_CASE("deferred .reg file replaces a slot; SetupComplete.cmd gets the import line once") {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"deferred";
    std::filesystem::remove_all(dir);
    const auto file = deferredRegFile(dir, DeferredScope::User);
    CHECK(file.filename() == L"firstlogon-user.reg");
    auto a = parseRegValue(L"HKCU\\Software\\T", L"v", L"dword:00000001");
    auto b = parseRegValue(L"HKCU\\Software\\T", L"v", L"dword:00000000");
    auto c = parseRegValue(L"HKCU\\Software\\T", L"w", L"\"s\"");
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    CHECK(deferredScope(*a) == DeferredScope::User);
    REQUIRE(updateDeferredRegFile(file, *a));
    REQUIRE(updateDeferredRegFile(file, *c));
    REQUIRE(updateDeferredRegFile(file, *b)); // same slot as a → replaced
    auto read = readRegFile(file);
    REQUIRE(read);
    REQUIRE(read->size() == 2);
    CHECK((*read)[0] == *c);
    CHECK((*read)[1] == *b);

    const auto cmd = dir / L"SetupComplete.cmd";
    {
        std::ofstream out(cmd, std::ios::binary);
        out << "@echo off\r\necho existing";
    }
    REQUIRE(ensureSetupCompleteImport(cmd));
    REQUIRE(ensureSetupCompleteImport(cmd));
    std::ifstream in(cmd, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Inserted right after "@echo off", before the user's own commands (which may end with exit).
    CHECK(text.starts_with("@echo off\r\nrem WinLove"));
    CHECK(text.find("echo existing") > text.find("setupcomplete.reg"));
    const std::string line = "setupcomplete.reg";
    CHECK(text.find(line) != std::string::npos);
    CHECK(text.find(line) == text.rfind(line));
    in.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("parseRegText: values under [-key] are skipped; comments never continue; REGEDIT4 hex(2) is ANSI") {
    auto writes = parseRegText(L"Windows Registry Editor Version 5.00\n[-HKCU\\Software\\Gone]\n\"x\"=dword:00000001\n"
                               L"; a comment ending in \\\n[HKCU\\Software\\Kept]\n\"y\"=\"v\"\n");
    REQUIRE(writes);
    REQUIRE(writes->size() == 2);
    CHECK((*writes)[0].kind == RegistryWrite::Kind::DeleteKey);
    CHECK((*writes)[1].key == L"HKCU\\Software\\Kept");

    auto legacy = parseRegText(L"REGEDIT4\n[HKCU\\Software\\A]\n\"P\"=hex(2):25,41,00\n");
    REQUIRE(legacy);
    REQUIRE(legacy->size() == 1);
    CHECK(formatRegValue(legacy->front()) == L"hex(2):25,00,41,00,00,00"); // "%A\0" as UTF-16
    CHECK_FALSE(parseRegValue(L"HKCU\\A", L"x", L"dword:-1"));
}
