// D-116: drivers Windows loads at boot and device filters never leave an image, whichever component
// a file belongs to (report 2026-11-07: fvevol.sys went with BitLocker, the system stopped at 0xc000000f).
#include "core/image/BootDrivers.h"

#include <doctest.h>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

using namespace wl::core;

TEST_CASE("boot drivers: a service's ImagePath names one file below the image root") {
    CHECK(driverImageFile(L"\\SystemRoot\\System32\\drivers\\fvevol.sys", L"fvevol") == L"windows\\system32\\drivers\\fvevol.sys");
    CHECK(driverImageFile(L"System32\\DRIVERS\\fvevol.sys", L"fvevol") == L"windows\\system32\\drivers\\fvevol.sys");
    CHECK(driverImageFile(L"system32\\drivers\\wd\\WdBoot.sys", L"WdBoot") == L"windows\\system32\\drivers\\wd\\wdboot.sys");
    CHECK(driverImageFile(L"\\??\\C:\\Windows\\system32\\drivers\\x.sys", L"x") == L"windows\\system32\\drivers\\x.sys");
    CHECK(driverImageFile(L"C:\\Program Files\\Vendor\\v.sys", L"v") == L"program files\\vendor\\v.sys");
    CHECK(driverImageFile(L"%SystemRoot%\\System32\\drivers\\y.sys", L"y") == L"windows\\system32\\drivers\\y.sys");
    CHECK(driverImageFile(L"\"\\SystemRoot\\System32\\drivers\\q.sys\"", L"q") == L"windows\\system32\\drivers\\q.sys");
    CHECK(driverImageFile(L"\\Windows\\System32\\drivers\\r.sys", L"r") == L"windows\\system32\\drivers\\r.sys");
    // None: what the loader uses then.
    CHECK(driverImageFile(L"", L"iorate") == L"windows\\system32\\drivers\\iorate.sys");
    CHECK(driverImageFile(L"", L"") == L"");
    // Not one plain file below the root.
    CHECK(driverImageFile(L"\\??\\GLOBALROOT\\Device\\x.sys", L"x") == L"");
    CHECK(driverImageFile(L"System32\\..\\..\\x.sys", L"x") == L"");
    CHECK(driverImageFile(L"System32\\drivers\\\\x.sys", L"x") == L"");
    CHECK(driverImageFile(L"%ProgramFiles%\\x.sys", L"x") == L"");
}

TEST_CASE("boot drivers: a path in the mounted image, the same way") {
    const std::filesystem::path mount = L"C:\\Users\\u\\AppData\\Local\\WinLove\\mount";
    CHECK(imageRelativePath(L"C:\\Users\\u\\AppData\\Local\\WinLove\\mount\\Windows\\System32\\drivers\\FVEVOL.SYS", mount) ==
          L"windows\\system32\\drivers\\fvevol.sys");
    CHECK(imageRelativePath(L"\\\\?\\C:\\Users\\u\\AppData\\Local\\WinLove\\mount\\Windows\\x.sys", mount) == L"windows\\x.sys");
    CHECK(imageRelativePath(L"C:\\Users\\u\\AppData\\Local\\WinLove\\mount2\\Windows\\x.sys", mount) == L"");
    CHECK(imageRelativePath(L"C:\\Users\\u\\AppData\\Local\\WinLove\\mount", mount) == L"");
    CHECK(imageRelativePath(L"D:\\Windows\\x.sys", mount) == L"");
}

TEST_CASE("boot drivers: REG_MULTI_SZ") {
    const std::wstring raw(L"fvevol\0iorate\0\0", 15);
    std::vector<std::uint8_t> data(reinterpret_cast<const std::uint8_t*>(raw.data()),
                                   reinterpret_cast<const std::uint8_t*>(raw.data()) + raw.size() * sizeof(wchar_t));
    CHECK(multiStrings(data) == std::vector<std::wstring>{L"fvevol", L"iorate"});
    CHECK(multiStrings({}).empty());
}

TEST_CASE("boot drivers: filters always stay; a boot driver goes only with its service") {
    const std::vector<BootDriver> drivers{
        {L"fvevol", L"windows\\system32\\drivers\\fvevol.sys", true, true},  // boot start + volume filter
        {L"scfilter", L"windows\\system32\\drivers\\scfilter.sys", false, true}, // a filter that starts on demand
        {L"WdBoot", L"windows\\system32\\drivers\\wdboot.sys", true, false}, // boot start only
        {L"ndiswan", L"windows\\system32\\drivers\\ndiswan.sys", true, false},
        {L"ndiswanlegacy", L"windows\\system32\\drivers\\ndiswan.sys", true, false}, // the same file
    };
    auto kept = keptDriverFiles(drivers, {});
    CHECK(kept.size() == 4);
    CHECK(kept.contains(L"windows\\system32\\drivers\\fvevol.sys"));
    CHECK(kept.contains(L"windows\\system32\\drivers\\scfilter.sys"));
    CHECK(kept.contains(L"windows\\system32\\drivers\\wdboot.sys"));

    // The step removes the services as well: nothing loads WdBoot any more; a filter's name stays in its class.
    kept = keptDriverFiles(drivers, {L"WDBOOT", L"fvevol", L"BDESVC", L"ndiswan"});
    CHECK_FALSE(kept.contains(L"windows\\system32\\drivers\\wdboot.sys"));
    CHECK(kept.contains(L"windows\\system32\\drivers\\fvevol.sys"));
    CHECK(kept.contains(L"windows\\system32\\drivers\\ndiswan.sys")); // ndiswanlegacy still uses it
    CHECK(keptFilterServices(drivers, {L"WDBOOT", L"FveVol", L"BDESVC"}) == std::vector<std::wstring>{L"fvevol"});
}

TEST_CASE("boot drivers: what a removal path is or holds") {
    CHECK(isBootFileType(L"x\\werkernel.SYS"));
    CHECK(isBootFileType(L"hal.dll"));
    CHECK(isBootFileType(L"ntoskrnl.exe"));
    CHECK(isBootFileType(L"winload.efi"));
    CHECK(isBootFileType(L"c_1254.NLS"));
    CHECK_FALSE(isBootFileType(L"readme.txt"));
    CHECK_FALSE(isBootFileType(L"x.sys.mui"));
    const auto dir = std::filesystem::temp_directory_path() / L"wl-boot-driver-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / L"sub");
    std::ofstream(dir / L"a.txt").put('x');
    std::ofstream(dir / L"a.dll").put('x');
    std::ofstream(dir / L"sub" / L"B.SYS").put('x');
    CHECK(bootFilesUnder(dir / L"a.txt").empty());
    CHECK(bootFilesUnder(dir / L"a.dll").size() == 1);
    CHECK(bootFilesUnder(dir / L"sub" / L"B.SYS").size() == 1);
    CHECK(bootFilesUnder(dir).size() == 2);
    CHECK(bootFilesUnder(dir / L"missing").empty());
    // Not a PE image, or none at all: no imports.
    CHECK(peImports(dir / L"a.dll").empty());
    CHECK(peImports(dir / L"missing.sys").empty());
    CHECK_FALSE(readApiSetSchema(dir / L"a.dll"));
    std::filesystem::remove_all(dir);
}

namespace {

std::filesystem::path system32() {
    wchar_t buffer[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length));
}

std::wstring lower_copy(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return text;
}

bool hasImport(const std::vector<std::wstring>& imports, std::wstring_view prefix) {
    for (const auto& name : imports) {
        if (_wcsnicmp(name.c_str(), prefix.data(), prefix.size()) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("boot drivers: a module's imports and the API set schema (this PC's Windows)") {
    // D-117: the kernel imports error reporting's kernel part through an API set.
    const auto kernel = peImports(system32() / L"ntoskrnl.exe");
    REQUIRE_FALSE(kernel.empty());
    CHECK(hasImport(kernel, L"pshed.dll")); // (hal.dll: merged into the kernel since 24H2)
    CHECK(hasImport(kernel, L"ext-ms-win-ntos-werkernel-"));

    auto schema = readApiSetSchema(system32() / L"apisetschema.dll");
    REQUIRE(schema);
    CHECK(schema->size() > 100);
    const auto wer = importHosts(*schema, L"EXT-MS-WIN-NTOS-WERKERNEL-l1-1-1.dll");
    CHECK(std::ranges::find(wer, std::wstring(L"werkernel.sys")) != wer.end());
    // Any minor version resolves the same way.
    CHECK(importHosts(*schema, L"ext-ms-win-ntos-werkernel-l1-1-0.dll") == wer);
    CHECK_FALSE(importHosts(*schema, L"api-ms-win-core-file-l1-1-0.dll").empty());
    CHECK(importHosts(*schema, L"api-ms-win-no-such-contract-l1-1-0.dll").empty());
    // A plain name is the file itself.
    CHECK(importHosts(*schema, L"MSRPC.sys") == std::vector<std::wstring>{L"msrpc.sys"});

    // Names held as text: winlogon.exe loads the telemetry client by name (24H2 and later).
    const auto named = namedModules(system32() / L"winlogon.exe");
    CHECK_FALSE(named.empty());
    CHECK(std::ranges::find(named, std::wstring(L"utcapi.dll")) != named.end());
    CHECK(std::ranges::find(named, std::wstring(L"userinit.exe")) != named.end());
    CHECK(std::ranges::all_of(named, [](const std::wstring& n) {
        return (n.ends_with(L".dll") || n.ends_with(L".exe")) && n == lower_copy(n);
    }));
}

TEST_CASE("boot drivers: removal keeps what the loader and the boot drivers import, transitively") {
    // A small image: the real kernel and schema, stand-ins for the rest.
    const auto root = std::filesystem::temp_directory_path() / L"wl-boot-closure-test";
    std::filesystem::remove_all(root);
    const auto sys32 = root / L"Windows" / L"System32";
    std::filesystem::create_directories(sys32 / L"drivers");
    std::filesystem::copy_file(system32() / L"ntoskrnl.exe", sys32 / L"ntoskrnl.exe");
    std::filesystem::copy_file(system32() / L"apisetschema.dll", sys32 / L"apisetschema.dll");
    for (const wchar_t* name : {L"werkernel.sys", L"msrpc.sys", L"unrelated.sys", L"fvevol.sys", L"wdboot.sys"}) {
        std::ofstream(sys32 / L"drivers" / name).put('x');
    }
    std::ofstream(sys32 / L"hal.dll").put('x');
    std::ofstream(sys32 / L"c_1254.nls").put('x');
    const std::vector<BootDriver> drivers{
        {L"fvevol", L"windows\\system32\\drivers\\fvevol.sys", true, true},
        {L"WdBoot", L"windows\\system32\\drivers\\wdboot.sys", true, false},
        {L"Nls\\CodePage\\1254", L"windows\\system32\\c_1254.nls", true, false},
    };
    auto kept = keptBootFiles(root, drivers, {L"WdBoot"});
    REQUIRE(kept);
    CHECK(kept->contains(L"windows\\system32\\ntoskrnl.exe"));
    CHECK(kept->contains(L"windows\\system32\\apisetschema.dll"));
    CHECK(kept->contains(L"windows\\system32\\hal.dll"));
    CHECK(kept->contains(L"windows\\system32\\c_1254.nls"));
    CHECK(kept->contains(L"windows\\system32\\drivers\\fvevol.sys"));
    REQUIRE(kept->contains(L"windows\\system32\\drivers\\werkernel.sys")); // ntoskrnl.exe → ext-ms-win-ntos-werkernel
    CHECK(kept->at(L"windows\\system32\\drivers\\werkernel.sys") == L"ntoskrnl.exe imports it");
    CHECK(kept->contains(L"windows\\system32\\drivers\\msrpc.sys"));
    CHECK_FALSE(kept->contains(L"windows\\system32\\drivers\\unrelated.sys"));
    // werkernel.sys is also an API set host: kept by the import walk first, the reason says so.
    CHECK(kept->at(L"windows\\system32\\drivers\\werkernel.sys") == L"ntoskrnl.exe imports it");
    CHECK_FALSE(kept->contains(L"windows\\system32\\drivers\\wdboot.sys")); // its service goes in the same step
    CHECK_FALSE(kept->contains(L"windows\\system32\\kdcom.dll"));           // not in this image

    // Past the loader: the shell and what it imports (explorer.exe → AEPIC.dll, report 2026-11-08).
    std::filesystem::copy_file(system32().parent_path() / L"explorer.exe", root / L"Windows" / L"explorer.exe");
    std::ofstream(sys32 / L"aepic.dll").put('x');
    kept = keptBootFiles(root, drivers, {});
    REQUIRE(kept);
    CHECK(kept->at(L"windows\\explorer.exe") == L"Windows starts it");
    if (hasImport(peImports(root / L"Windows" / L"explorer.exe"), L"aepic.dll")) {
        CHECK(kept->contains(L"windows\\system32\\aepic.dll"));
    }

    // What the sign-in chain loads by name (winlogon.exe → utcapi.dll) and Setup's sysprep modules.
    std::filesystem::copy_file(system32() / L"winlogon.exe", sys32 / L"winlogon.exe");
    std::ofstream(sys32 / L"utcapi.dll").put('x');
    std::ofstream(sys32 / L"provsysprep.dll").put('x');
    std::filesystem::create_directories(sys32 / L"Sysprep" / L"ActionFiles");
    std::ofstream(sys32 / L"Sysprep" / L"ActionFiles" / L"Specialize.xml")
        << "<sysprepInformation><imaging><sysprepModule methodName=\"ProvPackageSysprepSpecialize\" "
           "moduleName=\"$(runtime.system32)\\ProvSysprep.dll\"/></imaging></sysprepInformation>";
    CHECK(sysprepModules(root) == std::vector<std::wstring>{L"windows\\system32\\provsysprep.dll"});
    kept = keptBootFiles(root, drivers, {});
    REQUIRE(kept);
    CHECK(kept->at(L"windows\\system32\\provsysprep.dll") == L"Setup runs it (Sysprep\\ActionFiles)");
    const auto winlogonNames = namedModules(sys32 / L"winlogon.exe");
    if (std::ranges::find(winlogonNames, std::wstring(L"utcapi.dll")) != winlogonNames.end()) {
        CHECK(kept->at(L"windows\\system32\\utcapi.dll") == L"winlogon.exe loads it by name");
    }

    // No schema: the caller keeps every module.
    std::filesystem::remove(sys32 / L"apisetschema.dll");
    CHECK_FALSE(keptBootFiles(root, drivers, {}));
    std::filesystem::remove_all(root);
}

TEST_CASE("imports: a module that stays keeps what it imports or delay-loads (D-118)") {
    // feclient.dll delay-loads efscore.dll (EFS): the VM test of the D-117 image, every Store package failed
    // to register for a new account once EFS's files were gone.
    const auto delayed = peDelayImports(system32() / L"feclient.dll");
    REQUIRE(hasImport(delayed, L"efscore.dll"));
    CHECK_FALSE(hasImport(peImports(system32() / L"feclient.dll"), L"efscore.dll"));
    CHECK(peDelayImports(system32() / L"no-such-module.dll").empty());

    const auto root = std::filesystem::temp_directory_path() / L"wl-import-keep-test";
    std::filesystem::remove_all(root);
    const auto sys32 = root / L"Windows" / L"System32";
    std::filesystem::create_directories(sys32);
    std::filesystem::copy_file(system32() / L"apisetschema.dll", sys32 / L"apisetschema.dll");
    std::filesystem::copy_file(system32() / L"feclient.dll", sys32 / L"feclient.dll");
    std::ofstream(sys32 / L"efscore.dll").put('x');
    std::ofstream(sys32 / L"unrelated.dll").put('x');

    auto kept = keptImportedFiles(root, {L"windows\\system32\\efscore.dll", L"windows\\system32\\unrelated.dll"});
    REQUIRE(kept);
    REQUIRE(kept->contains(L"windows\\system32\\efscore.dll"));
    CHECK(kept->at(L"windows\\system32\\efscore.dll") == L"feclient.dll delay-loads it");
    CHECK_FALSE(kept->contains(L"windows\\system32\\unrelated.dll"));

    // The importer goes in the same step: nothing that stays needs it.
    kept = keptImportedFiles(root, {L"windows\\system32\\efscore.dll", L"windows\\system32\\feclient.dll"});
    REQUIRE(kept);
    CHECK(kept->empty());
    CHECK(keptImportedFiles(root, {})->empty());

    // No schema: the caller keeps every module.
    std::filesystem::remove(sys32 / L"apisetschema.dll");
    CHECK_FALSE(keptImportedFiles(root, {L"windows\\system32\\efscore.dll"}));
    std::filesystem::remove_all(root);
}

TEST_CASE("boot drivers: a real image's SYSTEM hive (WL_TEST_IMAGE_ROOT, lab only)") {
    wchar_t root[1024] = {};
    if (GetEnvironmentVariableW(L"WL_TEST_IMAGE_ROOT", root, 1024) == 0) {
        return; // an image root: <root>\Windows\System32\config\SYSTEM
    }
    auto drivers = findBootDrivers(std::filesystem::path(root));
    REQUIRE(drivers);
    MESSAGE(drivers->size() << " boot / filter drivers");
    const auto find = [&](std::wstring_view name) -> const BootDriver* {
        for (const auto& d : *drivers) {
            if (_wcsicmp(d.service.c_str(), std::wstring(name).c_str()) == 0) {
                return &d;
            }
        }
        return nullptr;
    };
    const auto* fvevol = find(L"fvevol");
    REQUIRE(fvevol != nullptr);
    CHECK(fvevol->early);
    CHECK(fvevol->filter);
    CHECK(fvevol->file == L"windows\\system32\\drivers\\fvevol.sys");
    const auto* volsnap = find(L"volsnap");
    REQUIRE(volsnap != nullptr);
    CHECK(volsnap->filter);
    CHECK(find(L"disk") != nullptr);
    CHECK(find(L"Ntfs") != nullptr);     // Start 3, "Boot File System"
    CHECK(find(L"stornvme") != nullptr); // Start 3 in the image, the system disk's driver on an NVMe PC
    CHECK(find(L"WerSvc") == nullptr); // a Win32 service started on demand
    const auto* eventLog = find(L"EventLog"); // D-117: one that starts by itself, its service DLL
    REQUIRE(eventLog != nullptr);
    CHECK(eventLog->early);
    CHECK(eventLog->file == L"windows\\system32\\wevtsvc.dll");
    const auto codePage = std::ranges::find_if(*drivers, [](const BootDriver& d) { return d.service.starts_with(L"Nls\\CodePage\\"); });
    REQUIRE(codePage != drivers->end());
    CHECK(codePage->early);

    // D-117: the closure over the real image.
    auto kept = keptBootFiles(std::filesystem::path(root), *drivers, {});
    REQUIRE(kept);
    MESSAGE(kept->size() << " boot files kept");
    CHECK(kept->contains(L"windows\\system32\\ntoskrnl.exe"));
    CHECK(kept->contains(codePage->file));
    if (std::filesystem::exists(std::filesystem::path(root) / L"Windows\\System32\\drivers\\werkernel.sys")) {
        CHECK(kept->contains(L"windows\\system32\\drivers\\werkernel.sys"));
    }
    if (find(L"vmbus") && std::filesystem::exists(std::filesystem::path(root) / L"Windows\\System32\\drivers\\winhv.sys")) {
        CHECK(kept->contains(L"windows\\system32\\drivers\\winhv.sys")); // vmbus.sys (boot start) imports it
    }
    // A program a session program names may go (lab, 2026-11-08: taskmgr.exe → the WARP JIT service).
    CHECK_FALSE(kept->contains(L"windows\\system32\\taskmgr.exe"));
    CHECK_FALSE(kept->contains(L"windows\\system32\\windows.warp.jitservice.exe"));
    CHECK(kept->contains(L"windows\\system32\\utcapi.dll"));        // winlogon.exe loads it by name
    CHECK(kept->contains(L"windows\\system32\\srpapi.dll"));        // an API set host (KernelBase's SRP checks)
    CHECK(kept->contains(L"windows\\system32\\provsysprep.dll"));   // Specialize.xml
}
