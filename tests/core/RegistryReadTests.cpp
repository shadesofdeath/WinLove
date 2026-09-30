// D-045: reading an offline image's registry with offreg.dll. The hives are built here with the
// same library (ORCreateHive / ORSaveHive work without admin), laid out like a mounted image.
#include "core/image/RegistryRead.h"

#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>

using namespace wl;
using core::OfflineRegistryReader;
using core::RegistryWrite;

namespace {

using ORHKEY = void*;
struct OffRegWriter {
    DWORD(WINAPI* createHive)(ORHKEY*) = nullptr;
    DWORD(WINAPI* createKey)(ORHKEY, PCWSTR, PWSTR, DWORD, PSECURITY_DESCRIPTOR, ORHKEY*, PDWORD) = nullptr;
    DWORD(WINAPI* setValue)(ORHKEY, PCWSTR, DWORD, const BYTE*, DWORD) = nullptr;
    DWORD(WINAPI* closeKey)(ORHKEY) = nullptr;
    DWORD(WINAPI* saveHive)(ORHKEY, PCWSTR, DWORD, DWORD) = nullptr;
    DWORD(WINAPI* closeHive)(ORHKEY) = nullptr;
    OffRegWriter() {
        const HMODULE m = LoadLibraryExW(L"offreg.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        REQUIRE(m != nullptr);
        createHive = reinterpret_cast<decltype(createHive)>(GetProcAddress(m, "ORCreateHive"));
        createKey = reinterpret_cast<decltype(createKey)>(GetProcAddress(m, "ORCreateKey"));
        setValue = reinterpret_cast<decltype(setValue)>(GetProcAddress(m, "ORSetValue"));
        closeKey = reinterpret_cast<decltype(closeKey)>(GetProcAddress(m, "ORCloseKey"));
        saveHive = reinterpret_cast<decltype(saveHive)>(GetProcAddress(m, "ORSaveHive"));
        closeHive = reinterpret_cast<decltype(closeHive)>(GetProcAddress(m, "ORCloseHive"));
        REQUIRE((createHive && createKey && setValue && closeKey && saveHive && closeHive));
    }
};

// A hive file with `values` (key, name, type, data) and bare `keys`.
struct Entry {
    const wchar_t* key;
    const wchar_t* name; // nullptr: just the key
    DWORD type = REG_DWORD;
    std::vector<BYTE> data;
};

void writeHive(const std::filesystem::path& file, const std::vector<Entry>& entries) {
    static const OffRegWriter api;
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::remove(file);
    ORHKEY hive = nullptr;
    REQUIRE(api.createHive(&hive) == ERROR_SUCCESS);
    for (const auto& e : entries) {
        // ORCreateKey makes one level at a time.
        ORHKEY key = hive;
        std::wstring path = e.key;
        for (std::size_t start = 0; start <= path.size();) {
            const std::size_t end = std::min(path.find(L'\\', start), path.size());
            ORHKEY child = nullptr;
            DWORD disposition = 0;
            REQUIRE(api.createKey(key, path.substr(start, end - start).c_str(), nullptr, 0, nullptr, &child,
                                  &disposition) == ERROR_SUCCESS);
            if (key != hive) {
                api.closeKey(key);
            }
            key = child;
            start = end + 1;
        }
        if (e.name) {
            REQUIRE(api.setValue(key, e.name, e.type, e.data.data(), static_cast<DWORD>(e.data.size())) == ERROR_SUCCESS);
        }
        api.closeKey(key);
    }
    REQUIRE(api.saveHive(hive, file.c_str(), 10, 0) == ERROR_SUCCESS);
    api.closeHive(hive);
}

std::vector<BYTE> dword(DWORD v) {
    return {static_cast<BYTE>(v), static_cast<BYTE>(v >> 8), static_cast<BYTE>(v >> 16), static_cast<BYTE>(v >> 24)};
}

std::vector<BYTE> text(std::wstring_view s, bool terminated = true) {
    std::vector<BYTE> out(reinterpret_cast<const BYTE*>(s.data()), reinterpret_cast<const BYTE*>(s.data() + s.size()));
    if (terminated) {
        out.push_back(0);
        out.push_back(0);
    }
    return out;
}

RegistryWrite parse(const wchar_t* key, const wchar_t* name, const wchar_t* value) {
    auto w = core::parseRegValue(key, name, value);
    REQUIRE(w);
    return *w;
}

std::filesystem::path fakeImage() {
    const auto root = std::filesystem::temp_directory_path() / L"wl-tests" / L"registry-read";
    std::filesystem::remove_all(root);
    const auto config = root / L"Windows" / L"System32" / L"config";
    writeHive(config / L"SOFTWARE", {
        {L"Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry", REG_DWORD, dword(0)},
        {L"Test\\Strings", L"Terminated", REG_SZ, text(L"abc")},
        {L"Test\\Strings", L"Bare", REG_SZ, text(L"abc", false)},
        {L"Test\\Strings", L"", REG_SZ, text(L"default")},
        {L"Test\\EmptyKey", nullptr},
    });
    writeHive(config / L"SYSTEM", {
        {L"Select", L"Current", REG_DWORD, dword(2)},
        {L"ControlSet001\\Control\\Test", L"Value", REG_DWORD, dword(1)},
        {L"ControlSet002\\Control\\Test", L"Value", REG_DWORD, dword(2)},
    });
    // No Users\Default\NTUSER.DAT: HKCU reads find nothing, they do not fail.
    return root;
}

} // namespace

TEST_CASE("registry read: values, key existence and holds() on an offline image") {
    const auto image = fakeImage();
    OfflineRegistryReader reader(image);

    auto value = reader.value(L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", L"AllowTelemetry");
    REQUIRE(value);
    REQUIRE(value->has_value());
    CHECK((*value)->type == REG_DWORD);
    CHECK((*value)->data == dword(0));

    auto missing = reader.value(L"HKLM\\SOFTWARE\\Policies\\Nope", L"X");
    REQUIRE(missing);
    CHECK_FALSE(missing->has_value());

    // CurrentControlSet follows Select\Current (2 here), not ControlSet001.
    auto ccs = reader.value(L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Test", L"Value");
    REQUIRE(ccs);
    REQUIRE(ccs->has_value());
    CHECK((*ccs)->data == dword(2));

    auto user = reader.value(L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"HideFileExt");
    REQUIRE(user);
    CHECK_FALSE(user->has_value());

    CHECK(reader.keyExists(L"HKLM\\SOFTWARE\\Test\\EmptyKey").value());
    CHECK_FALSE(reader.keyExists(L"HKLM\\SOFTWARE\\Test\\Nope").value());

    const auto telemetry = L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection";
    CHECK(reader.holds(parse(telemetry, L"AllowTelemetry", L"dword:00000000")).value());
    CHECK_FALSE(reader.holds(parse(telemetry, L"AllowTelemetry", L"dword:00000001")).value());
    CHECK_FALSE(reader.holds(parse(telemetry, L"AllowTelemetry", L"\"0\"")).value()); // type differs
    CHECK_FALSE(reader.holds(parse(telemetry, L"AllowTelemetry", L"-")).value());
    CHECK(reader.holds(parse(telemetry, L"Other", L"-")).value());

    // Strings compare without their terminating null; the default value is name "".
    CHECK(reader.holds(parse(L"HKLM\\SOFTWARE\\Test\\Strings", L"Terminated", L"\"abc\"")).value());
    CHECK(reader.holds(parse(L"HKLM\\SOFTWARE\\Test\\Strings", L"Bare", L"\"abc\"")).value());
    CHECK(reader.holds(parse(L"HKLM\\SOFTWARE\\Test\\Strings", L"", L"\"default\"")).value());
    CHECK_FALSE(reader.holds(parse(L"HKLM\\SOFTWARE\\Test\\Strings", L"Bare", L"\"abcd\"")).value());

    CHECK(reader.holds(parse(L"HKLM\\SOFTWARE\\Test\\EmptyKey", L"", L"[+]")).value());
    CHECK_FALSE(reader.holds(parse(L"HKLM\\SOFTWARE\\Test\\EmptyKey", L"", L"[-]")).value());
    CHECK(reader.holds(parse(L"HKLM\\SOFTWARE\\Test\\Gone", L"", L"[-]")).value());

    // Keys with no hive in the image are never "there"; unsupported roots are errors.
    CHECK_FALSE(reader.holds(parse(L"HKCC\\Software\\X", L"Y", L"dword:00000001")).value());
    CHECK_FALSE(reader.value(L"HKLM\\SAM\\X", L"Y").has_value());
}

TEST_CASE("registry read: a damaged hive file is an error, a missing one is not") {
    const auto root = std::filesystem::temp_directory_path() / L"wl-tests" / L"registry-read-bad";
    std::filesystem::remove_all(root);
    const auto config = root / L"Windows" / L"System32" / L"config";
    std::filesystem::create_directories(config);
    std::ofstream(config / L"SOFTWARE", std::ios::binary) << "not a hive";
    OfflineRegistryReader reader(root);
    CHECK_FALSE(reader.value(L"HKLM\\SOFTWARE\\X", L"Y").has_value());
    auto system = reader.value(L"HKLM\\SYSTEM\\Select", L"Current"); // no SYSTEM file at all
    REQUIRE(system);
    CHECK_FALSE(system->has_value());
}
