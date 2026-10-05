// P11: .reg syntax, key normalization / offline hive mapping, ChangeSet round trip.
#include "core/image/RegistryEdit.h"
#include "core/image/RegistryInput.h"
#include "core/image/Services.h"

#include <doctest.h>

#include <windows.h>

#include <fstream>

using namespace wl;
using namespace wl::core;

TEST_CASE("normalizeRegistryKey accepts long and short roots") {
    CHECK(normalizeRegistryKey(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\X\\") == L"HKLM\\SOFTWARE\\X");
    CHECK(normalizeRegistryKey(L"hkcu\\Software") == L"HKCU\\Software");
    CHECK(normalizeRegistryKey(L"HKEY_CURRENT_CONFIG\\X") == L"HKCC\\X");
    CHECK(normalizeRegistryKey(L"HKEY_DYN_DATA\\X").empty());
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
    CHECK(mapOfflineKey(L"HKU\\S-1-5-18\\Control Panel")->hive == OfflineHiveFile::DotDefault); // LocalSystem
    CHECK_FALSE(mapOfflineKey(L"HKLM\\SAM\\SAM"));
    CHECK_FALSE(mapOfflineKey(L"HKU\\S-1-5-21-1\\Software"));
    // No hive in the image, but the post-setup import can still write them.
    CHECK_FALSE(mapOfflineKey(L"HKCC\\Software\\Fonts"));
    CHECK(isPostSetupOnlyKey(L"HKEY_CURRENT_CONFIG\\Software\\Fonts"));
    CHECK(isPostSetupOnlyKey(L"HKU\\S-1-5-19\\Software\\X"));
    CHECK_FALSE(isPostSetupOnlyKey(L"HKU\\S-1-5-21-1\\Software"));
    CHECK_FALSE(isPostSetupOnlyKey(L"HKLM\\SAM\\SAM"));
    CHECK_FALSE(isPostSetupOnlyKey(L"HKLM\\SOFTWARE\\X"));
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
    auto del = registryWriteFrom(L"HKLM\\SOFTWARE\\X::", L"[-]"); // the form older presets hold
    REQUIRE(del);
    CHECK(del->kind == RegistryWrite::Kind::DeleteKey);
    // A slot of its own: "[-key]" followed by the key's default value keeps both operations.
    CHECK(registryTarget(*del) == L"HKLM\\SOFTWARE\\X\\\\::");
    const auto delBack = registryWriteFrom(registryTarget(*del), formatRegValue(*del));
    REQUIRE(delBack);
    CHECK(*delBack == *del);
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

TEST_CASE("a key without values is created: parse, target, .reg text round trip") {
    auto writes = parseRegText(L"Windows Registry Editor Version 5.00\n"
                               L"[HKEY_CLASSES_ROOT\\AllFilesystemObjects\\shellex\\ContextMenuHandlers\\{C2FBB630}]\n\n"
                               L"[HKCU\\Software\\A]\n@=\"d\"\n[HKCU\\Software\\A]\n[HKCU\\Software\\B]\n");
    REQUIRE(writes);
    REQUIRE(writes->size() == 4);
    const auto& w = *writes;
    CHECK(w[0].kind == RegistryWrite::Kind::CreateKey);
    CHECK(w[0].key == L"HKCR\\AllFilesystemObjects\\shellex\\ContextMenuHandlers\\{C2FBB630}");
    CHECK(w[1].kind == RegistryWrite::Kind::Set);
    CHECK(w[2].kind == RegistryWrite::Kind::CreateKey);
    CHECK(w[3].key == L"HKCU\\Software\\B");
    // Not the same ChangeSet slot as the key's default value.
    CHECK(registryTarget(w[1]) == L"HKCU\\Software\\A::");
    CHECK(registryTarget(w[2]) == L"HKCU\\Software\\A\\::");
    CHECK(formatRegValue(w[2]) == L"[+]");
    const auto back = registryWriteFrom(registryTarget(w[2]), formatRegValue(w[2]));
    REQUIRE(back);
    CHECK(*back == w[2]);
    auto again = parseRegText(formatRegText(w));
    REQUIRE(again);
    CHECK(*again == w);
}

TEST_CASE("deferred .reg files: appended per write, compacted on close; hooks once per run") {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"deferred";
    std::filesystem::remove_all(dir);
    const auto file = deferredRegFile(dir, DeferredScope::User);
    CHECK(file.filename() == L"firstlogon-user.reg");
    auto a = parseRegValue(L"HKCU\\Software\\T", L"v", L"dword:00000001");
    auto b = parseRegValue(L"HKCU\\Software\\T", L"v", L"dword:00000000");
    auto c = parseRegValue(L"HKCU\\Software\\T", L"w", L"\"s\"");
    auto made = parseRegValue(L"HKCU\\Software\\T", L"", L"[+]");
    auto def = parseRegValue(L"HKCU\\Software\\T", L"", L"\"default\"");
    auto machine = parseRegValue(L"HKLM\\SOFTWARE\\T", L"m", L"dword:00000001");
    auto config = parseRegValue(L"HKEY_CURRENT_CONFIG\\Software\\Fonts", L"LogPixels", L"dword:00000060");
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    REQUIRE(made);
    REQUIRE(def);
    REQUIRE(machine);
    REQUIRE(config);
    CHECK(deferredScope(*a) == DeferredScope::User);
    CHECK(deferredScope(*config) == DeferredScope::Machine);
    const auto cmd = dir / L"Windows" / L"Setup" / L"Scripts" / L"SetupComplete.cmd";
    {
        DeferredRegistry deferred(dir);
        REQUIRE(deferred.record(*a));
        CHECK(deferred.takeUserHook());       // the caller adds the RunOnce value now…
        REQUIRE(deferred.record(*c));
        CHECK_FALSE(deferred.takeUserHook()); // …and only once per run
        REQUIRE(deferred.record(*made));
        REQUIRE(deferred.record(*def));       // same empty name as the created key: both stay
        REQUIRE(deferred.record(*b));         // same slot as a
        // Before close: every write is on disk already; importing in order gives the same values.
        auto appended = readRegFile(file);
        REQUIRE(appended);
        REQUIRE(appended->size() == 5);
        CHECK(appended->front() == *a);
        CHECK(appended->back() == *b);

        CHECK_FALSE(std::filesystem::exists(cmd));
        REQUIRE(deferred.record(*machine));
        REQUIRE(deferred.record(*config));
        CHECK(std::filesystem::exists(cmd));
    }
    auto read = readRegFile(file);
    REQUIRE(read);
    REQUIRE(read->size() == 4); // a was superseded by b
    CHECK((*read)[0] == *c);
    CHECK((*read)[1] == *made);
    CHECK((*read)[2] == *def);
    CHECK((*read)[3] == *b);
    auto machineRead = readRegFile(deferredRegFile(dir, DeferredScope::Machine));
    REQUIRE(machineRead);
    REQUIRE(machineRead->size() == 2);
    CHECK((*machineRead)[1] == *config);
    {
        // A later run on the same image continues the files.
        DeferredRegistry deferred(dir);
        REQUIRE(deferred.record(*a));
    }
    read = readRegFile(file);
    REQUIRE(read);
    REQUIRE(read->size() == 4);
    CHECK(read->back() == *a);
}

TEST_CASE("SetupComplete.cmd gets the import line once, before the user's own commands") {
    const auto dir = std::filesystem::temp_directory_path() / L"wl-tests" / L"setupcomplete";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
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

TEST_CASE("typed registry values: each type from the dialog's text, and back") {
    using core::RegValueType;
    auto make = [](RegValueType type, std::wstring_view data, std::wstring_view name = L"v") {
        return core::registryWriteFromInput(LR"(HKEY_CURRENT_USER\Software\X)", name, type, data);
    };
    auto dword = make(RegValueType::Dword, L"0x1F");
    REQUIRE(dword);
    CHECK(dword->key == LR"(HKCU\Software\X)");
    CHECK(core::formatRegValue(*dword) == L"dword:0000001f");
    CHECK(make(RegValueType::Dword, L"4294967295"));
    CHECK_FALSE(make(RegValueType::Dword, L"4294967296"));
    CHECK_FALSE(make(RegValueType::Dword, L"12a"));
    CHECK_FALSE(make(RegValueType::Dword, L""));
    auto qword = make(RegValueType::Qword, L"1");
    REQUIRE(qword);
    CHECK(core::formatRegValue(*qword) == L"hex(b):01,00,00,00,00,00,00,00");
    auto text = make(RegValueType::String, L"a \"b\"");
    REQUIRE(text);
    CHECK(core::formatRegValue(*text) == L"\"a \\\"b\\\"\"");
    CHECK(make(RegValueType::String, L""));
    auto multi = make(RegValueType::MultiString, L"one;two;;three;");
    REQUIRE(multi);
    CHECK(core::registryWriteInput(*multi).data == L"one;two;;three");
    auto binary = make(RegValueType::Binary, L"01 0a,FF");
    REQUIRE(binary);
    CHECK(core::formatRegValue(*binary) == L"hex:01,0a,ff");
    CHECK_FALSE(make(RegValueType::Binary, L"0x1"));
    auto delKey = make(RegValueType::DeleteKey, L"", L"ignored");
    REQUIRE(delKey);
    CHECK(core::formatRegValue(*delKey) == L"[-]");
    CHECK(delKey->name.empty());
    CHECK(core::formatRegValue(*make(RegValueType::DeleteValue, L"")) == L"-");

    CHECK_FALSE(core::registryWriteFromInput(L"HKLM", L"v", RegValueType::String, L"x"));
    CHECK_FALSE(core::registryWriteFromInput(LR"(HKXX\Software)", L"v", RegValueType::String, L"x"));
    CHECK_FALSE(core::registryWriteFromInput(LR"(HKCU\A::B)", L"v", RegValueType::String, L"x"));

    for (const auto& w : {*dword, *qword, *text, *multi, *binary, *delKey}) {
        const auto input = core::registryWriteInput(w);
        auto again = core::registryWriteFromInput(w.key, w.name, input.type, input.data);
        REQUIRE(again);
        CHECK(*again == w);
    }
}
