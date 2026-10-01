#include "core/image/DeepRemoval.h"

#include "base/Log.h"
#include "core/image/OffReg.h"
#include "core/image/OfflineHive.h"
#include "core/system/BackupFiles.h"
#include "core/system/FileLocks.h"
#include "core/system/Privileges.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <functional>
#include <set>

namespace wl::core {

namespace {

// Device classes nothing a current PC needs depends on. Anything else (network, storage, USB, HID,
// display, audio, Bluetooth, printers, smart cards, cameras …) is refused, whatever a preset says.
constexpr const wchar_t* kDeepRemovableClasses[] = {
    L"{4D36E96D-E325-11CE-BFC1-08002BE10318}", // Modem
    L"{6D807884-7D21-11CF-801C-08002BE10318}", // TapeDrive
    L"{CE5939AE-EBDE-11D0-B181-0000F8753EC4}", // MediumChanger
    L"{4D36E980-E325-11CE-BFC1-08002BE10318}", // FloppyDisk
    L"{4D36E969-E325-11CE-BFC1-08002BE10318}", // FDC (floppy controller)
    L"{6BDD1FC1-810F-11D0-BEC7-08002BE2092F}", // 1394 host controllers
    L"{7EBEFBC0-3200-11D2-B4C2-00A0C9697D07}", // 61883 (FireWire A/V)
    L"{C06FF265-AE09-48F0-812C-16753D7CBA83}", // AVC
    L"{D48179BE-EC20-11D1-B6B8-00C04FA372A7}", // SBP2 (FireWire storage)
    L"{4D36E977-E325-11CE-BFC1-08002BE10318}", // PCMCIA
    L"{C243FFBD-3AFC-45E9-B3D3-2BA18BC7EBC5}", // BarcodeScanner
    L"{5AEA001D-9372-4ED7-97F3-B79BF15A53C5}", // OPOSLegacyDevice
    L"{13E42DFA-85D9-424D-8646-28A70F864F9C}", // RemotePosDevice
};

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

// A file / key name taken from the image: one plain component, nothing that walks out of a folder.
bool plainName(std::wstring_view name) {
    return !name.empty() && name != L"." && name != L".." && std::ranges::all_of(name, [](wchar_t c) {
        return c < 128 && (std::iswalnum(c) != 0 || c == L'_' || c == L'-' || c == L'.');
    });
}

std::wstring regString(const std::vector<std::uint8_t>& data) {
    std::wstring text(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    return text;
}

// "<arch>_dual_<inf>_<token>_<version>_<culture>_<hash>" (WinSxS folder, manifest, COMPONENTS key)
// or "dual_<inf>_…" (deployment key) of one of `infs`.
bool isDriverComponent(std::wstring_view name, const std::set<std::wstring>& infs, bool deployment) {
    const std::wstring n = lower(name);
    std::size_t start = 0;
    if (!deployment) {
        const std::size_t arch = n.find(L'_');
        if (arch == std::wstring::npos) {
            return false;
        }
        start = arch + 1;
    }
    if (n.compare(start, 5, L"dual_") != 0) {
        return false;
    }
    start += 5;
    return std::ranges::any_of(infs, [&](const std::wstring& inf) {
        return n.compare(start, inf.size(), inf) == 0 && n.size() > start + inf.size() && n[start + inf.size()] == L'_';
    });
}

bool under(const std::filesystem::path& path, const std::filesystem::path& folder) {
    const std::wstring p = lower(path.wstring());
    std::wstring f = lower(folder.wstring());
    if (!f.ends_with(L'\\')) {
        f += L'\\';
    }
    return p.starts_with(f);
}

void filesUnder(const std::filesystem::path& folder, std::vector<std::filesystem::path>& out) {
    for (const auto& name : backupListFiles(folder)) {
        out.push_back(folder / name);
    }
    for (const auto& name : backupListFolders(folder)) {
        filesUnder(folder / name, out);
    }
}

// Every other name of a (hard-linked) file, as full paths.
std::vector<std::filesystem::path> otherLinks(const std::filesystem::path& file) {
    std::vector<std::filesystem::path> links;
    wchar_t volume[MAX_PATH];
    if (!GetVolumePathNameW(file.c_str(), volume, MAX_PATH)) {
        return links;
    }
    std::wstring root = volume;
    while (!root.empty() && root.back() == L'\\') {
        root.pop_back();
    }
    std::wstring name(1024, L'\0');
    DWORD length = static_cast<DWORD>(name.size());
    HANDLE find = FindFirstFileNameW(file.c_str(), 0, &length, name.data());
    if (find == INVALID_HANDLE_VALUE) {
        return links;
    }
    do {
        std::filesystem::path full = root + std::wstring(name.c_str());
        if (_wcsicmp(full.c_str(), file.c_str()) != 0) {
            links.push_back(std::move(full));
        }
        length = static_cast<DWORD>(name.size());
    } while (FindNextFileNameW(find, &length, name.data()));
    FindClose(find);
    return links;
}

// Deletes the subkeys of `parent` the predicate picks. Returns how many went.
Result<std::size_t> deleteSubkeys(HKEY parent, const std::function<bool(const std::wstring&)>& pick) {
    std::size_t removed = 0;
    for (const auto& name : subkeyNames(parent)) {
        if (!pick(name)) {
            continue;
        }
        if (const LSTATUS status = deleteKeyTree(parent, name.c_str()); status != ERROR_SUCCESS) {
            return std::unexpected(registryError(status, L"could not delete registry key", name));
        }
        ++removed;
    }
    return removed;
}

struct Plan {
    std::set<std::wstring> infs;     // lower case
    std::set<std::wstring> packages; // lower case
    std::vector<std::filesystem::path> files;
    std::set<std::wstring> sysFiles; // lower case, under System32\drivers: their services go too
    std::uint64_t bytes = 0;
};

// One DriverDatabase (the DRIVERS hive holds most packages, SYSTEM the boot-capable ones).
Result<std::size_t> editDriverDatabase(HKEY hiveRoot, const Plan& plan) {
    std::size_t removed = 0;
    HKEY raw = nullptr;
    const REGSAM all = KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE | DELETE;
    if (const LSTATUS status = openKeyForWrite(hiveRoot, L"DriverDatabase", all, raw); status != ERROR_SUCCESS) {
        return status == ERROR_FILE_NOT_FOUND ? Result<std::size_t>{0}
                                              : std::unexpected(registryError(status, L"could not open the driver database", L"DriverDatabase"));
    }
    RegKey db(raw);
    struct Part {
        const wchar_t* key;
        std::function<bool(const std::wstring&)> pick;
    };
    const Part parts[] = {
        {L"DriverPackages", [&](const std::wstring& n) { return plan.packages.contains(lower(n)); }},
        {L"DriverInfFiles", [&](const std::wstring& n) { return plan.infs.contains(lower(n)); }},
    };
    for (const auto& part : parts) {
        HKEY sub = nullptr;
        if (openKeyForWrite(db.get(), part.key, all, sub) != ERROR_SUCCESS) {
            continue;
        }
        RegKey key(sub);
        auto gone = deleteSubkeys(key.get(), part.pick);
        if (!gone) {
            return std::unexpected(gone.error());
        }
        removed += *gone;
    }
    // DeviceIds\<hardware id>: one value per INF that serves it.
    HKEY ids = nullptr;
    if (openKeyForWrite(db.get(), L"DeviceIds", all, ids) == ERROR_SUCCESS) {
        RegKey idsKey(ids);
        for (const auto& id : idsKey.subkeys()) {
            HKEY one = nullptr;
            if (openKeyForWrite(idsKey.get(), id.c_str(), KEY_QUERY_VALUE | KEY_SET_VALUE, one) != ERROR_SUCCESS) {
                continue;
            }
            RegKey idKey(one);
            for (const auto& inf : plan.infs) {
                if (RegDeleteValueW(idKey.get(), inf.c_str()) == ERROR_SUCCESS) {
                    ++removed;
                }
            }
        }
    }
    // DriverFiles\<file>: its default value names the INF(s) that ship it; ours alone → key goes.
    HKEY files = nullptr;
    if (openKeyForWrite(db.get(), L"DriverFiles", all, files) == ERROR_SUCCESS) {
        RegKey filesKey(files);
        auto gone = deleteSubkeys(filesKey.get(), [&](const std::wstring& name) {
            HKEY one = nullptr;
            if (RegOpenKeyExW(filesKey.get(), name.c_str(), 0, KEY_QUERY_VALUE, &one) != ERROR_SUCCESS) {
                return false;
            }
            RegKey key(one);
            std::vector<std::wstring> owners = key.multiString(nullptr);
            if (owners.empty()) {
                if (auto single = key.string(nullptr)) {
                    owners.push_back(*single);
                }
            }
            return !owners.empty() && std::ranges::all_of(owners, [&](const std::wstring& o) { return plan.infs.contains(lower(o)); });
        });
        if (!gone) {
            return std::unexpected(gone.error());
        }
        removed += *gone;
    }
    return removed;
}

Result<std::size_t> editComponentsHive(const std::filesystem::path& config, const Plan& plan) {
    auto hive = OfflineHive::load(config / L"COMPONENTS");
    if (!hive) {
        return std::unexpected(hive.error());
    }
    std::size_t removed = 0;
    const REGSAM all = KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE | DELETE;
    for (const auto& [path, deployment] : {std::pair{L"DerivedData\\Components", false}, std::pair{L"CanonicalData\\Deployments", true}}) {
        HKEY raw = nullptr;
        if (const LSTATUS status = openKeyForWrite(hive->root(), path, all, raw); status != ERROR_SUCCESS) {
            return std::unexpected(registryError(status, L"could not open the component store", path));
        }
        RegKey key(raw);
        auto gone = deleteSubkeys(key.get(), [&](const std::wstring& name) { return isDriverComponent(name, plan.infs, deployment); });
        if (!gone) {
            return std::unexpected(gone.error());
        }
        removed += *gone;
    }
    return removed;
}

Result<std::size_t> editDriversHive(const std::filesystem::path& config, const Plan& plan) {
    auto hive = OfflineHive::load(config / L"DRIVERS");
    if (!hive) {
        return std::unexpected(hive.error());
    }
    return editDriverDatabase(hive->root(), plan);
}

// SYSTEM: its own DriverDatabase, and the services whose driver file went with the payload
// (ImagePath "…\drivers\<file>.sys").
Result<std::size_t> editSystemHive(const std::filesystem::path& config, const Plan& plan) {
    auto hive = OfflineHive::load(config / L"SYSTEM");
    if (!hive) {
        return std::unexpected(hive.error());
    }
    auto database = editDriverDatabase(hive->root(), plan);
    if (!database) {
        return std::unexpected(database.error());
    }
    std::size_t removed = *database;
    if (plan.sysFiles.empty()) {
        return removed;
    }
    for (const auto& set : subkeyNames(hive->root())) {
        if (!lower(set).starts_with(L"controlset")) {
            continue;
        }
        HKEY raw = nullptr;
        if (openKeyForWrite(hive->root(), (set + L"\\Services").c_str(), KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | DELETE, raw) !=
            ERROR_SUCCESS) {
            continue;
        }
        RegKey services(raw);
        auto gone = deleteSubkeys(services.get(), [&](const std::wstring& name) {
            HKEY one = nullptr;
            if (RegOpenKeyExW(services.get(), name.c_str(), 0, KEY_QUERY_VALUE, &one) != ERROR_SUCCESS) {
                return false;
            }
            RegKey key(one);
            const auto image = key.string(L"ImagePath");
            if (!image) {
                return false;
            }
            const std::wstring path = lower(*image);
            const std::size_t slash = path.find_last_of(L'\\');
            const std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
            return path.find(L"\\drivers\\") != std::wstring::npos && plan.sysFiles.contains(file);
        });
        if (!gone) {
            return std::unexpected(gone.error());
        }
        removed += *gone;
    }
    return removed;
}

} // namespace

bool isDeepRemovableClass(std::wstring_view classGuid) {
    const std::wstring g = normalizeClassGuid(classGuid);
    return std::ranges::any_of(kDeepRemovableClasses, [&](const wchar_t* c) { return g == c; });
}

std::wstring normalizeClassGuid(std::wstring_view text) {
    std::wstring t(text);
    while (!t.empty() && std::iswspace(t.front())) {
        t.erase(t.begin());
    }
    while (!t.empty() && std::iswspace(t.back())) {
        t.pop_back();
    }
    if (t.size() == 36) {
        t = L"{" + t + L"}";
    }
    if (t.size() != 38 || t.front() != L'{' || t.back() != L'}') {
        return {};
    }
    for (std::size_t i = 1; i < 37; ++i) {
        const bool dash = i == 9 || i == 14 || i == 19 || i == 24;
        if (dash ? t[i] != L'-' : std::iswxdigit(t[i]) == 0) {
            return {};
        }
        t[i] = static_cast<wchar_t>(std::towupper(t[i]));
    }
    return t;
}

std::wstring driverPackageClass(const std::vector<std::uint8_t>& version) {
    if (version.size() < 24) {
        return {};
    }
    auto byte = [&](std::size_t i) { return static_cast<unsigned>(version[i]); };
    const unsigned d1 = byte(8) | (byte(9) << 8) | (byte(10) << 16) | (byte(11) << 24);
    const unsigned d2 = byte(12) | (byte(13) << 8);
    const unsigned d3 = byte(14) | (byte(15) << 8);
    return std::format(L"{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}", d1, d2, d3, byte(16), byte(17),
                       byte(18), byte(19), byte(20), byte(21), byte(22), byte(23));
}

Result<std::vector<InboxDriver>> findInboxDrivers(const std::filesystem::path& mountDir, const std::vector<std::wstring>& classGuids) {
    std::set<std::wstring> wanted;
    for (const auto& g : classGuids) {
        if (isDeepRemovableClass(g)) {
            wanted.insert(normalizeClassGuid(g));
        }
    }
    if (classGuids.empty()) {
        wanted.insert(std::begin(kDeepRemovableClasses), std::end(kDeepRemovableClasses));
    }
    // Two driver databases: DRIVERS holds most packages, SYSTEM the boot-capable ones (floppy,
    // FDC, PCMCIA, 1394 … on 25H2).
    std::vector<InboxDriver> drivers;
    const auto config = mountDir / L"Windows" / L"System32" / L"config";
    for (const wchar_t* file : {L"DRIVERS", L"SYSTEM"}) {
        auto hive = OffRegKey::openHive(config / file);
        if (!hive) {
            return std::unexpected(hive.error());
        }
        const auto packages = hive->open(L"DriverDatabase\\DriverPackages");
        for (const auto& package : packages.subkeys()) {
            InboxDriver driver{package, {}, {}};
            for (const auto& value : packages.open(package).values()) {
                if (value.name == L"Version") {
                    driver.classGuid = driverPackageClass(value.data);
                } else if (value.name.empty() && value.type == REG_SZ) {
                    driver.inf = regString(value.data);
                }
            }
            // Class INFs (c_modem.inf …) define the class itself: they stay.
            if (wanted.contains(driver.classGuid) && plainName(driver.inf) && plainName(package) &&
                !lower(driver.inf).starts_with(L"c_")) {
                drivers.push_back(std::move(driver));
            }
        }
    }
    std::ranges::sort(drivers, [](const InboxDriver& a, const InboxDriver& b) { return a.package < b.package; });
    return drivers;
}

Result<DeepRemovalResult> deepRemoveDrivers(const std::filesystem::path& mountDir, const std::vector<InboxDriver>& drivers,
                                            const TaskContext& task) {
    DeepRemovalResult result;
    if (drivers.empty()) {
        return result;
    }
    Plan plan;
    for (const auto& d : drivers) {
        if (!isDeepRemovableClass(d.classGuid) || !plainName(d.inf) || !plainName(d.package) || lower(d.inf).starts_with(L"c_")) {
            return fail(ErrorCode::InvalidArgument, L"not a driver deep removal may take", d.package);
        }
        plan.infs.insert(lower(d.inf));
        plan.packages.insert(lower(d.package));
    }
    for (const wchar_t* privilege : {SE_BACKUP_NAME, SE_RESTORE_NAME}) {
        if (auto enabled = enablePrivilege(privilege); !enabled) {
            return std::unexpected(enabled.error());
        }
    }
    const auto windows = mountDir / L"Windows";
    const auto config = windows / L"System32" / L"config";
    const auto sxs = windows / L"WinSxS";
    const auto store = windows / L"System32" / L"DriverStore";
    const auto repository = store / L"FileRepository";
    const auto drivers32 = windows / L"System32" / L"drivers";

    // 1. Everything on disk, found before anything is touched.
    std::vector<std::filesystem::path> payloads;
    for (const auto& folder : backupListFolders(sxs)) {
        if (isDriverComponent(folder, plan.infs, false)) {
            payloads.push_back(sxs / folder);
            plan.bytes += backupFolderSize(sxs / folder);
        }
    }
    for (const auto& file : backupListFiles(sxs / L"Manifests")) {
        if (isDriverComponent(file, plan.infs, false)) {
            plan.files.push_back(sxs / L"Manifests" / file);
        }
    }
    std::set<std::wstring> seen;
    for (const auto& payload : payloads) {
        std::vector<std::filesystem::path> files;
        filesUnder(payload, files);
        for (const auto& file : files) {
            for (auto& link : otherLinks(file)) {
                // Inside the image only, outside what goes whole anyway.
                if (!under(link, mountDir) || under(link, sxs) || under(link, repository) || !seen.insert(lower(link.wstring())).second) {
                    continue;
                }
                if (under(link, drivers32) && lower(link.extension().wstring()) == L".sys") {
                    plan.sysFiles.insert(lower(link.filename().wstring()));
                }
                plan.files.push_back(std::move(link));
            }
        }
        plan.files.push_back(payload);
    }
    for (const auto& d : drivers) {
        plan.files.push_back(repository / d.package);
        plan.files.push_back(windows / L"INF" / d.inf);
        plan.files.push_back(windows / L"INF" / (std::filesystem::path(d.inf).stem().wstring() + L".pnf"));
    }
    for (const auto& culture : backupListFolders(store)) {
        if (_wcsicmp(culture.c_str(), L"FileRepository") == 0) {
            continue;
        }
        for (const auto& d : drivers) {
            plan.files.push_back(store / culture / (d.inf + L"_loc"));
        }
    }
    task.report(0.2, L"plan");
    if (auto go = task.cancel.check(); !go) {
        return std::unexpected(go.error());
    }

    // 2. The registry: driver database, component store, services.
    auto driversEdited = editDriversHive(config, plan);
    if (!driversEdited) {
        return std::unexpected(driversEdited.error());
    }
    auto componentsEdited = editComponentsHive(config, plan);
    if (!componentsEdited) {
        return std::unexpected(componentsEdited.error());
    }
    auto systemEdited = editSystemHive(config, plan);
    if (!systemEdited) {
        return std::unexpected(systemEdited.error());
    }
    result.registry = *driversEdited + *componentsEdited + *systemEdited;
    task.report(0.5, L"registry");

    // 3. The files (whatever is gone already counts as done).
    for (std::size_t i = 0; i < plan.files.size(); ++i) {
        const auto& path = plan.files[i];
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        if (auto gone = forceRemoveEntry(path); !gone) {
            return std::unexpected(gone.error());
        }
        ++result.files;
        task.report(0.5 + 0.5 * static_cast<double>(i + 1) / static_cast<double>(plan.files.size()), L"files");
    }
    result.drivers = drivers.size();
    result.bytes = plan.bytes;
    log::info("cbs", std::format(L"deep removal: {} driver(s), {} file(s)/folder(s), {} registry entr(ies), {} bytes of payload",
                                 result.drivers, result.files, result.registry, result.bytes));
    return result;
}

} // namespace wl::core
