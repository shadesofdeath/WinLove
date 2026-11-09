#include "core/image/SystemComponents.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/BootDrivers.h"
#include "core/image/ComponentStore.h"
#include "core/image/DeepRemoval.h"
#include "core/image/ImageInventory.h"
#include "core/image/RegistryEdit.h"
#include "core/image/dism/Appx.h"
#include "core/system/BackupFiles.h"
#include "core/system/FileLocks.h"
#include "core/system/Files.h"
#include "core/system/Privileges.h"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <map>

namespace wl::core {

namespace {

using Json = nlohmann::json;

// Folders no recipe may remove whole (compared without case, backslashes only). Top-level
// folders are already out: a path needs two levels.
constexpr std::array<std::wstring_view, 14> kProtected = {
    L"windows\\system32",         L"windows\\syswow64",          L"windows\\winsxs",
    L"windows\\servicing",        L"windows\\system32\\config",  L"windows\\system32\\drivers",
    L"windows\\system32\\driverstore", L"windows\\system32\\catroot", L"windows\\boot",
    L"windows\\fonts",            L"windows\\inf",               L"windows\\systemapps",
    L"program files\\windowsapps", L"users\\default",
};

// Package families no recipe may remove. Measured (D-075): without TroubleShooting (WDI, the
// troubleshooters, DiagTrack) the installed system never leaves the first boot's specialize pass
// (black screen, spinner, no logo) — 3 of 3 lab installs with it hung, 11 of 11 without it passed.
constexpr std::array<std::wstring_view, 2> kNeverRemovedFamilies = {
    L"microsoft-onecore-troubleshooting-package",
    L"microsoft-onecore-troubleshooting-wow64-package",
};

bool neverRemoved(std::wstring_view family) {
    return std::ranges::find(kNeverRemovedFamilies, std::wstring_view(text::lower(family))) != kNeverRemovedFamilies.end();
}

// What the "telemetry" component does since D-075 (resources/catalog/components.json): the
// services are switched off instead. A recipe of an older preset that removed the package gets these.
constexpr std::array<std::pair<std::wstring_view, std::wstring_view>, 3> kTelemetryOff = {{
    {L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\DiagTrack::Start", L"dword:00000004"},
    {L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\dmwappushservice::Start", L"dword:00000004"},
    {L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection::AllowTelemetry", L"dword:00000000"},
}};

// "a\b/c" → {"a","b","c"}; an empty part (leading, trailing or doubled separator) is kept so the
// caller can refuse it.
std::vector<std::wstring_view> splitPath(std::wstring_view path) {
    std::vector<std::wstring_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            parts.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

Result<void> validatePath(std::wstring_view path) {
    auto refuse = [&](const wchar_t* why) { return fail(ErrorCode::InvalidArgument, why, std::wstring(path)); };
    if (path.empty()) {
        return refuse(L"component path is empty");
    }
    const auto parts = splitPath(path);
    std::wstring normalized;
    for (const auto part : parts) {
        if (part.empty() || part == L"." || part == L"..") {
            return refuse(L"component path must be relative, without empty, '.' or '..' parts");
        }
        if (part.find_first_of(L":*?\"<>|") != std::wstring_view::npos || part.back() == L' ' || part.back() == L'.') {
            return refuse(L"component path has a character a file name cannot have");
        }
        if (!normalized.empty()) {
            normalized += L'\\';
        }
        normalized += text::lower(part);
    }
    if (parts.size() < 2) {
        return refuse(L"component path is too close to the image root");
    }
    if (std::ranges::find(kProtected, std::wstring_view(normalized)) != kProtected.end()) {
        return refuse(L"component path is a folder Windows needs");
    }
    return {};
}

} // namespace

// ---- recipe ------------------------------------------------------------------------------------

std::string componentRecipeToJson(const ComponentRecipe& recipe) {
    Json doc{{"title", utf8::fromWide(recipe.title)}};
    auto list = [](const std::vector<std::wstring>& items) {
        Json array = Json::array();
        for (const auto& item : items) {
            array.push_back(utf8::fromWide(item));
        }
        return array;
    };
    if (!recipe.packages.empty()) {
        doc["packages"] = list(recipe.packages);
    }
    if (!recipe.paths.empty()) {
        doc["paths"] = list(recipe.paths);
    }
    if (!recipe.driverClasses.empty()) {
        doc["driverClasses"] = list(recipe.driverClasses);
    }
    if (!recipe.driverPackages.empty()) {
        doc["driverPackages"] = list(recipe.driverPackages);
    }
    if (!recipe.appx.empty()) {
        doc["appx"] = list(recipe.appx);
    }
    if (!recipe.storeNames.empty()) {
        doc["storeNames"] = list(recipe.storeNames);
        if (recipe.storeBits != 0) {
            doc["storeBits"] = recipe.storeBits;
        }
    }
    if (!recipe.services.empty()) {
        doc["services"] = list(recipe.services);
    }
    if (recipe.afterUpdates) {
        doc["afterUpdates"] = true;
    }
    if (!recipe.registry.empty()) {
        Json writes = Json::array();
        for (const auto& write : recipe.registry) {
            writes.push_back({{"target", utf8::fromWide(registryTarget(write))},
                              {"value", utf8::fromWide(formatRegValue(write))}});
        }
        doc["registry"] = std::move(writes);
    }
    return doc.dump();
}

Result<ComponentRecipe> componentRecipeFromJson(std::string_view json) {
    const auto doc = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(ErrorCode::ParseError, L"not a component recipe");
    }
    ComponentRecipe recipe;
    try {
        recipe.title = utf8::toWide(doc.value("title", std::string{}));
        for (const auto& item : doc.value("packages", Json::array())) {
            recipe.packages.push_back(utf8::toWide(item.get<std::string>()));
        }
        for (const auto& item : doc.value("paths", Json::array())) {
            recipe.paths.push_back(utf8::toWide(item.get<std::string>()));
        }
        for (const auto& item : doc.value("driverClasses", Json::array())) {
            recipe.driverClasses.push_back(utf8::toWide(item.get<std::string>()));
        }
        for (const auto& item : doc.value("driverPackages", Json::array())) {
            recipe.driverPackages.push_back(utf8::toWide(item.get<std::string>()));
        }
        for (const auto& item : doc.value("appx", Json::array())) {
            recipe.appx.push_back(utf8::toWide(item.get<std::string>()));
        }
        for (const auto& item : doc.value("storeNames", Json::array())) {
            recipe.storeNames.push_back(utf8::toWide(item.get<std::string>()));
        }
        recipe.storeBits = static_cast<std::uint8_t>(doc.value("storeBits", 0));
        for (const auto& item : doc.value("services", Json::array())) {
            recipe.services.push_back(utf8::toWide(item.get<std::string>()));
        }
        recipe.afterUpdates = doc.value("afterUpdates", false);
        for (const auto& item : doc.value("registry", Json::array())) {
            auto write = registryWriteFrom(utf8::toWide(item.value("target", std::string{})),
                                           utf8::toWide(item.value("value", std::string{})));
            if (!write) {
                return std::unexpected(write.error());
            }
            recipe.registry.push_back(std::move(*write));
        }
    } catch (const Json::exception& e) {
        return fail(ErrorCode::ParseError, L"malformed component recipe", utf8::toWide(e.what()));
    }
    // A preset saved before D-075 still asks for the package: what the component does now instead.
    if (std::erase_if(recipe.packages, [](const std::wstring& family) { return neverRemoved(family); }) > 0) {
        log::warn("components", L"the TroubleShooting package stays (removing it hangs the first boot, D-075): "
                                L"the telemetry services are switched off instead · " + recipe.title);
        for (const auto& [target, value] : kTelemetryOff) {
            auto write = registryWriteFrom(std::wstring(target), std::wstring(value));
            if (write && std::ranges::find(recipe.registry, *write) == recipe.registry.end()) {
                recipe.registry.push_back(std::move(*write));
            }
        }
    }
    return recipe;
}

std::wstring componentTitle(std::wstring_view operationValue) {
    const auto doc = Json::parse(utf8::fromWide(operationValue), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return {};
    }
    const auto title = doc.find("title");
    return title != doc.end() && title->is_string() ? utf8::toWide(title->get<std::string>()) : std::wstring();
}

namespace {
// D-113: what a preset can never take, whatever it says (prefixes of store identity names).
constexpr std::wstring_view kProtectedStore[] = {
    L"microsoft-windows-kernel", L"microsoft-windows-os-kernel", L"microsoft-windows-ntdll",
    L"microsoft-windows-kernelbase", L"microsoft-windows-kernel32", L"microsoft-windows-win32k",
    L"microsoft-windows-user32", L"microsoft-windows-gdi32", L"microsoft-windows-advapi32",
    L"microsoft-windows-combase", L"microsoft-windows-com-base", L"microsoft-windows-rpc",
    L"microsoft-windows-csrss", L"microsoft-windows-smss", L"microsoft-windows-winlogon",
    L"microsoft-windows-lsa", L"microsoft-windows-security-lsa", L"microsoft-windows-authui",
    L"microsoft-windows-logonui", L"microsoft-windows-shell32", L"microsoft-windows-explorer",
    L"microsoft-windows-shellexperiencehost", L"microsoft-windows-bootenvironment",
    L"microsoft-windows-servicingstack", L"microsoft-windows-servicing", L"microsoft-windows-cbs",
    L"microsoft-windows-trustedinstaller", L"microsoft-windows-services-svchost",
    L"microsoft-windows-services-servicecontrollermanager", L"microsoft-windows-registry",
    L"microsoft-windows-ntfs", L"microsoft-windows-pnp", L"microsoft-onecore-pnp",
    L"microsoft-windows-crypt32", L"microsoft-windows-cryptbase", L"microsoft-windows-bcrypt",
    L"microsoft-windows-terminalservices-localsessionmanager", L"microsoft-windows-ie-clientnetworkprotocolimplementation",
    L"microsoft-windows-ie-runtimeutilities", L"microsoft-windows-ie-pluggablehandlersandsecurityzones",
    L"microsoft-windows-wmi-core", L"microsoft-windows-application-experience-apphelp",
    L"microsoft-windows-application-experience-mitigations", L"microsoft-windows-coreui",
    // D-120: what the reference tool keeps out of the components it removes (its definitions, read from the
    // binary: the "Modern App support" core it never removes, the text input service it locks for
    // apps / search / Wi-Fi, the Start menu's own packages, the feature configuration it excludes
    // from Windows Insider, the packages it keeps for the servicing stack). Ours, by rule: a
    // removal whose patterns reach one of these leaves it in place.
    L"microsoft-onecore-accessibility-experience", L"microsoft-windows-ui-xaml-controls",
    L"microsoft-onecore-applicationmodel-datatransfer", L"microsoft-windows-windowui", L"microsoft-windows-windowsui",
    L"microsoft-windows-xaml", L"microsoft-windows-creddialogcontroller", L"microsoft-windows-appx-deployment",
    L"microsoft-windows-onecoreuapcommonproxystub", L"microsoft-windows-appmodel-staterepository",
    L"microsoft-windows-applicationmodel-processmodel", L"microsoft-windows-securitymitigationsbroker",
    L"microsoft-windows-directui", L"microsoft-windows-tabletpc-platform-input-core",
    L"microsoft-windows-tabletpc-platform-input-service-client", L"microsoft-windows-tabletpc-platform-comruntime",
    L"microsoft-windows-tabletpc-journal", L"microsoft-windows-flighting-featureconfiguration",
    L"microsoft-windows-flighting-onesettingsclient", L"microsoft-windows-flighting-settings",
    L"microsoft-windows-cortana-brokeredapi", L"microsoft-windows-errorreportingfaults",
    L"microsoft-windows-securestartup-filterdriver", L"microsoft-windows-update-orchestratorcore-oobeexpedite",
};
constexpr std::wstring_view kProtectedServices[] = {
    L"rpcss", L"rpceptmapper", L"dcomlaunch", L"lsm", L"power", L"plugplay", L"brokerinfrastructure",
    L"samss", L"eventlog", L"profsvc", L"usermanager", L"coremessagingregistrar", L"staterepository",
    L"trustedinstaller", L"cryptsvc", L"dhcp", L"dnscache", L"nsi", L"mpssvc", L"bfe", L"gpsvc",
    L"winmgmt", L"systemeventsbroker", L"timebrokersvc", L"keyiso", L"appxsvc",
    L"textinputmanagementservice", // D-120: Start, Settings, search and Wi-Fi sign-in type through it
    // D-121: the rest of what the reference tool's Services page locks for "basic functionality" (TokenBroker
    // goes only with its own files, `aad`, which four guards hold). fvevol and rdyboost are volume
    // filters: a filter the volume class names without its key does not boot.
    L"deviceassociationservice", L"appreadiness", L"wlidsvc", L"windowstrustedrt", L"fvevol", L"rdyboost",
    L"dxgkrnl", L"wdf01000",
};
} // namespace

// D-120: whole names (a prefix would take ASP.NET's own system.web.mobile / .entity along). Windows
// PowerShell's ConvertFrom-Json stands on System.Web.Extensions → System.Web: without them WinLove's
// setup screens come up empty (D-117 fixed the catalog; an older preset still names them).
constexpr std::wstring_view kProtectedStoreExact[] = {
    L"system.web", L"system.web.extensions", L"system.web.services", L"system.web.regularexpressions",
    L"system.web.applicationservices",
};

bool isProtectedStoreName(std::wstring_view name) {
    std::wstring n(name);
    for (auto& c : n) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return std::ranges::any_of(kProtectedStore, [&](std::wstring_view prefix) { return n.starts_with(prefix); }) ||
           std::ranges::find(kProtectedStoreExact, std::wstring_view(n)) != std::end(kProtectedStoreExact);
}

bool isProtectedService(std::wstring_view name) {
    std::wstring n(name);
    for (auto& c : n) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return std::ranges::find(kProtectedServices, std::wstring_view(n)) != std::end(kProtectedServices);
}

Result<void> validateComponentRecipe(const ComponentRecipe& recipe) {
    if (recipe.packages.empty() && recipe.paths.empty() && recipe.registry.empty() && recipe.driverClasses.empty() &&
        recipe.driverPackages.empty() && recipe.appx.empty() && recipe.storeNames.empty() && recipe.services.empty()) {
        return fail(ErrorCode::InvalidArgument, L"component recipe does nothing", recipe.title);
    }
    for (const auto& path : recipe.paths) {
        if (auto ok = validatePath(path); !ok) {
            return ok;
        }
    }
    for (const auto& family : recipe.packages) {
        // A family is matched as "<family>~": it must be a whole package name.
        const bool plain = !family.empty() && std::ranges::all_of(family, [](wchar_t c) {
            return c < 128 && (std::iswalnum(c) != 0 || c == L'-' || c == L'_' || c == L'.');
        });
        if (!plain) {
            return fail(ErrorCode::InvalidArgument, L"not a package family name", family);
        }
        if (neverRemoved(family)) {
            return fail(ErrorCode::InvalidArgument, L"a package Windows cannot finish setup without (D-075)", family);
        }
    }
    for (const auto& write : recipe.registry) {
        if (auto mapped = mapOfflineKey(write.key); !mapped) {
            return std::unexpected(mapped.error());
        }
    }
    for (const auto& name : recipe.appx) {
        const bool plain = !name.empty() && name.size() <= 64 && std::ranges::all_of(name, [](wchar_t c) {
            return c < 128 && (std::iswalnum(c) != 0 || c == L'.' || c == L'-');
        });
        if (!plain) {
            return fail(ErrorCode::InvalidArgument, L"not an app name", name);
        }
    }
    for (const auto& package : recipe.driverPackages) {
        const bool plain = !package.empty() && package.size() <= 128 && std::ranges::all_of(package, [](wchar_t c) {
            return c < 128 && (std::iswalnum(c) != 0 || c == L'.' || c == L'_' || c == L'-');
        });
        if (!plain) {
            return fail(ErrorCode::InvalidArgument, L"not a driver package name", package);
        }
    }
    for (const auto& name : recipe.storeNames) {
        // Identity names as the store has them, lower case: letters, digits, ". _ -", and "( )" or an inner
        // space ("microsoft-windows-microsoft-data-access-components-(mdac)-ado15-dll", "…-base technologies-…").
        const bool plain = !name.empty() && name.size() <= 160 && name.find(L"..") == std::wstring::npos &&
                           name.front() != L' ' && name.back() != L' ' && std::ranges::all_of(name, [](wchar_t c) {
                               return c < 128 && (std::iswlower(c) != 0 || std::iswdigit(c) != 0 || c == L'.' ||
                                                  c == L'_' || c == L'-' || c == L'(' || c == L')' || c == L' ');
                           });
        if (!plain) {
            return fail(ErrorCode::InvalidArgument, L"not a store component name", name);
        }
        if (isProtectedStoreName(name)) {
            return fail(ErrorCode::InvalidArgument, L"a component Windows cannot start without (D-113)", name);
        }
    }
    if (recipe.storeBits != 0 && recipe.storeBits != 32 && recipe.storeBits != 64) {
        return fail(ErrorCode::InvalidArgument, L"storeBits is 0, 32 or 64", recipe.title);
    }
    for (const auto& name : recipe.services) {
        const bool plain = !name.empty() && name.size() <= 80 && std::ranges::all_of(name, [](wchar_t c) {
            return c < 128 && (std::iswalnum(c) != 0 || c == L'_' || c == L'-' || c == L'.');
        });
        if (!plain) {
            return fail(ErrorCode::InvalidArgument, L"not a service name", name);
        }
        if (isProtectedService(name)) {
            return fail(ErrorCode::InvalidArgument, L"a service Windows cannot start without (D-113)", name);
        }
    }
    // Deep removal never reaches beyond the legacy device classes, whatever a preset asks for.
    for (const auto& guid : recipe.driverClasses) {
        if (!isDeepRemovableClass(guid)) {
            return fail(ErrorCode::InvalidArgument, L"not a device class deep removal may take", guid);
        }
    }
    return {};
}

Result<std::filesystem::path> resolveImagePath(const std::filesystem::path& mountDir, std::wstring_view relative) {
    if (auto ok = validatePath(relative); !ok) {
        return std::unexpected(ok.error());
    }
    const auto parts = splitPath(relative);
    std::filesystem::path current = mountDir;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        current /= std::wstring(parts[i]);
        if (i + 1 < parts.size() && isReparsePoint(current)) {
            return fail(ErrorCode::InvalidArgument, L"component path goes through a link (it may leave the image)",
                        current.wstring());
        }
    }
    return current;
}

// ---- CBS packages ------------------------------------------------------------------------------

std::wstring_view cbsPackageFamily(std::wstring_view identity) {
    return identity.substr(0, identity.find(L'~'));
}

bool cbsLanguageNeutral(std::wstring_view identity) {
    // Name ~ publicKeyToken ~ architecture ~ language ~ version
    std::size_t at = 0;
    for (int field = 0; field < 3; ++field) {
        at = identity.find(L'~', at);
        if (at == std::wstring_view::npos) {
            return true;
        }
        ++at;
    }
    return at >= identity.size() || identity[at] == L'~';
}

std::vector<CbsPackage> cbsRemovalOrder(const std::vector<std::wstring>& families, const std::vector<CbsPackage>& all) {
    std::vector<CbsPackage> order;
    for (const auto& package : all) {
        if (package.state < kCbsStaged) {
            continue; // absent / resolved: nothing of it is in the image
        }
        const std::wstring_view family = cbsPackageFamily(package.identity);
        const bool wanted = std::ranges::any_of(families, [&](const std::wstring& f) {
            return text::iequals(f, family);
        });
        if (wanted) {
            order.push_back(package);
        }
    }
    std::ranges::stable_sort(order, [](const CbsPackage& a, const CbsPackage& b) {
        const bool an = cbsLanguageNeutral(a.identity);
        const bool bn = cbsLanguageNeutral(b.identity);
        if (an != bn) {
            return !an; // language packages first
        }
        return a.state > b.state; // what is installed before staged leftovers
    });
    return order;
}

std::vector<CbsPackage> readCbsPackages(HKEY packagesKey) {
    std::vector<CbsPackage> packages;
    for (auto& name : subkeyNames(packagesKey)) {
        HKEY raw = nullptr;
        if (RegOpenKeyExW(packagesKey, name.c_str(), 0, KEY_QUERY_VALUE, &raw) != ERROR_SUCCESS) {
            continue;
        }
        const RegKey key(raw);
        const auto visibility = key.dword(L"Visibility").value_or(0);
        const auto state = key.dword(L"CurrentState").value_or(0);
        packages.push_back({std::move(name), visibility, state});
    }
    return packages;
}

Result<void> unlockCbsPackage(HKEY packagesKey, const std::wstring& identity) {
    HKEY raw = nullptr;
    if (const LSTATUS status = openKeyForWrite(packagesKey, identity.c_str(), KEY_SET_VALUE | KEY_QUERY_VALUE, raw);
        status != ERROR_SUCCESS) {
        return std::unexpected(registryError(status, L"could not open the package's servicing key", identity));
    }
    RegKey key(raw);
    if (key.dword(L"Visibility") != 1u) {
        const DWORD visible = 1;
        if (const LSTATUS status = RegSetValueExW(key.get(), L"Visibility", 0, REG_DWORD,
                                                  reinterpret_cast<const BYTE*>(&visible), sizeof(visible));
            status != ERROR_SUCCESS) {
            return std::unexpected(registryError(status, L"could not make the package visible", identity));
        }
    }
    HKEY ownersRaw = nullptr;
    const LSTATUS opened = openKeyForWrite(
        key.get(), L"Owners", DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE, ownersRaw);
    if (opened == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (opened != ERROR_SUCCESS) {
        return std::unexpected(registryError(opened, L"could not open the package's Owners key", identity));
    }
    const RegKey owners(ownersRaw);
    if (const LSTATUS status = RegDeleteTreeW(owners.get(), nullptr); status != ERROR_SUCCESS) {
        return std::unexpected(registryError(status, L"could not clear the package's Owners key", identity));
    }
    if (!deleteKeyByHandle(owners.get())) {
        // Emptied is enough: a package with no owner listed is nobody's child.
        log::debug("cbs", L"Owners key emptied but not deleted: " + identity);
    }
    return {};
}

Result<std::vector<CbsPackage>> readCbsPackages(const std::filesystem::path& mountDir) {
    auto hive = OfflineHive::load(hiveFilePath(mountDir, OfflineHiveFile::Software));
    if (!hive) {
        return std::unexpected(hive.error());
    }
    auto packages = RegKey::open(hive->root(), kCbsPackagesKey);
    if (!packages) {
        return std::unexpected(packages.error());
    }
    return readCbsPackages(packages->get()); // `packages` closes before `hive` unloads
}

// ---- presence ----------------------------------------------------------------------------------

ComponentPresence probeComponent(const std::filesystem::path& mountDir, const ComponentRecipe& recipe,
                                 const ComponentStoreIndex* store) {
    ComponentPresence presence;
    (void)enablePrivilege(SE_BACKUP_NAME); // Program Files folders of an image can be closed to administrators
    for (const auto& relative : recipe.paths) {
        const auto path = resolveImagePath(mountDir, relative);
        if (!path) {
            continue;
        }
        // The directory entry, not the file: it carries the reparse tag. A mounted WIM serves its
        // untouched files as reparse points of its own kind — those ARE the files, and counting
        // them as links reported OneDriveSetup.exe as 0 bytes on the first real run. Only
        // junctions / symlinks (name surrogates) are links.
        WIN32_FIND_DATAW data{};
        const HANDLE find =
            FindFirstFileExW(path->c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }
        FindClose(find);
        presence.present = true;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IsReparseTagNameSurrogate(data.dwReserved0)) {
            continue; // removed as a link, nothing behind it is counted
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            presence.size += backupFolderSize(*path);
        } else {
            presence.size += (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        }
    }
    if (store && !recipe.driverClasses.empty()) {
        if (const auto drivers = store->drivers(recipe.driverClasses); !drivers.empty()) {
            presence.present = true;
            presence.size += store->driverBytes(drivers);
        }
    }
    if (store && !recipe.packages.empty()) {
        if (std::ranges::any_of(recipe.packages, [&](const std::wstring& f) { return store->installed(f); })) {
            presence.present = true;
            // A file the paths list is usually a hard link into WinSxS: the same bytes, once.
            presence.size = std::max(presence.size, store->exclusiveBytes(recipe.packages));
        }
    }
    return presence;
}

// ---- removal -----------------------------------------------------------------------------------

namespace {

// Unlocks the recipe's packages and applies its registry writes. The caller has closed the DISM
// session: nothing else may hold the image's hives.
Result<std::vector<CbsPackage>> editHives(const std::filesystem::path& mountDir, const ComponentRecipe& recipe) {
    std::vector<CbsPackage> targets;
    if (!recipe.packages.empty()) {
        auto hive = OfflineHive::load(hiveFilePath(mountDir, OfflineHiveFile::Software));
        if (!hive) {
            return std::unexpected(hive.error());
        }
        auto packages = RegKey::open(hive->root(), kCbsPackagesKey);
        if (!packages) {
            return std::unexpected(packages.error());
        }
        targets = cbsRemovalOrder(recipe.packages, readCbsPackages(packages->get()));
        for (const auto& package : targets) {
            if (auto unlocked = unlockCbsPackage(packages->get(), package.identity); !unlocked) {
                return std::unexpected(unlocked.error());
            }
        }
        log::info("cbs", std::format(L"{} package(s) unlocked for removal", targets.size()));
    } // key, then hive, released here
    if (!recipe.registry.empty()) {
        OfflineRegistry registry(mountDir);
        for (const auto& write : recipe.registry) {
            if (auto applied = registry.apply(write); !applied) {
                return std::unexpected(applied.error());
            }
        }
    }
    return targets;
}

// After a failed removal: is the package still there? nullopt when DISM cannot list them.
std::optional<bool> stillInstalled(DismSession& session, const std::wstring& identity) {
    auto packages = session.packages();
    if (!packages) {
        return std::nullopt;
    }
    return std::ranges::any_of(*packages, [&](const PackageEntry& p) {
        return _wcsicmp(p.name.c_str(), identity.c_str()) == 0 && p.state != ServicingState::NotPresent &&
               p.state != ServicingState::Removed && p.state != ServicingState::UninstallPending;
    });
}

} // namespace

Result<void> removeComponent(DismSession& session, const ComponentRecipe& requested, const TaskContext& task, bool packagesAllowed) {
    // D-120: a preset written before a name was kept still names it; that name stays and the rest
    // of the recipe goes (it used to fail the whole step).
    ComponentRecipe given = requested;
    {
        std::vector<std::wstring> kept;
        for (const auto& name : given.storeNames) {
            if (isProtectedStoreName(name)) {
                kept.push_back(name);
            }
        }
        for (const auto& name : given.services) {
            if (isProtectedService(name)) {
                kept.push_back(name);
            }
        }
        if (!kept.empty()) {
            std::erase_if(given.storeNames, [](const std::wstring& name) { return isProtectedStoreName(name); });
            std::erase_if(given.services, [](const std::wstring& name) { return isProtectedService(name); });
            std::wstring list;
            for (const auto& name : kept) {
                list += (list.empty() ? L"" : L", ") + name;
            }
            log::warn("cbs", given.title + L": kept, Windows needs them (D-120): " + list);
            if (given.packages.empty() && given.paths.empty() && given.registry.empty() && given.driverClasses.empty() &&
                given.driverPackages.empty() && given.appx.empty() && given.storeNames.empty() && given.services.empty()) {
                return {};
            }
        }
    }
    if (auto valid = validateComponentRecipe(given); !valid) {
        return valid;
    }
    // A closed image (shrunk WinSxS, report 2026-11-07): the packages stay — not unlocked, not
    // removed; the files, registry, services and apps of the recipe still go.
    std::optional<ComponentRecipe> trimmed;
    if (!packagesAllowed && !given.packages.empty()) {
        trimmed = given;
        trimmed->packages.clear();
        const auto& t = *trimmed;
        if (t.paths.empty() && t.registry.empty() && t.storeNames.empty() && t.services.empty() && t.driverClasses.empty() &&
            t.driverPackages.empty() && t.appx.empty()) {
            return fail(ErrorCode::Unsupported,
                        L"skipped: the component is CBS packages only and this image takes no more package servicing",
                        given.title, static_cast<std::int32_t>(0x800F0830));
        }
        log::warn("cbs", given.title + L": its packages stay (the image takes no more package servicing); the rest goes");
    }
    ComponentRecipe recipe = trimmed ? *trimmed : given;
    // D-119: a font Windows' own interface and its web engines fall back to never goes (Arial, Times
    // New Roman, Tahoma …: without them Setup's screens stopped and OOBE looped, VM 2026-11-08). A
    // font recipe names its family through the Fonts values it deletes; its files go with them.
    {
        const std::wstring fontsKey = normalizeRegistryKey(L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts");
        const auto coreFontValue = [&](const RegistryWrite& write) {
            return write.kind == RegistryWrite::Kind::DeleteValue && _wcsicmp(write.key.c_str(), fontsKey.c_str()) == 0 &&
                   isCoreFontFamily(fontFamily(write.name));
        };
        if (std::ranges::any_of(recipe.registry, coreFontValue)) {
            std::erase_if(recipe.registry, coreFontValue);
            std::erase_if(recipe.paths, [](const std::wstring& path) {
                return text::lower(path).starts_with(L"windows\\fonts\\");
            });
            log::warn("cbs", recipe.title + L": the font stays, Windows' interface and its web engines fall back to it (D-119)");
            if (recipe.paths.empty() && recipe.registry.empty() && recipe.packages.empty() && recipe.storeNames.empty() &&
                recipe.services.empty() && recipe.driverClasses.empty() && recipe.driverPackages.empty() && recipe.appx.empty()) {
                return {};
            }
        }
    }
    const std::filesystem::path mountDir = session.mountPath();
    // Every path is resolved before anything is touched: one refused path stops the whole recipe.
    std::vector<std::filesystem::path> paths;
    for (const auto& relative : recipe.paths) {
        auto path = resolveImagePath(mountDir, relative);
        if (!path) {
            return std::unexpected(path.error());
        }
        paths.push_back(std::move(*path));
    }
    // D-116 / D-117: a path that is (or holds) a file Windows needs before it starts stays: a boot
    // driver, a device filter, the loader's own files and every module these import.
    if (std::ranges::any_of(paths, [](const std::filesystem::path& path) { return !bootFilesUnder(path).empty(); })) {
        session.suspend(); // offreg reads SYSTEM: DISM must let go of it
        auto boot = findBootDrivers(mountDir);
        if (auto reopened = session.reload(); !reopened) {
            return reopened;
        }
        auto kept = boot ? keptBootFiles(mountDir, *boot, recipe.services) : std::unexpected(boot.error());
        std::erase_if(paths, [&](const std::filesystem::path& path) {
            for (const auto& file : bootFilesUnder(path)) {
                if (!kept) {
                    log::warn("cbs", std::format(L"{}: {} stays, boot files not readable: {}", recipe.title, path.wstring(),
                                                 describe(kept.error())));
                    return true;
                }
                if (const auto found = kept->find(imageRelativePath(file, mountDir)); found != kept->end()) {
                    log::warn("cbs", std::format(L"{}: {} stays, Windows needs {} before it starts ({}, D-116/D-117)", recipe.title,
                                                 path.wstring(), file.filename().wstring(), found->second));
                    return true;
                }
            }
            return false;
        });
        // D-118: nor one a module that stays imports or delay-loads (files outside these paths).
        std::vector<std::wstring> going;
        for (const auto& path : paths) {
            for (const auto& file : bootFilesUnder(path)) {
                going.push_back(imageRelativePath(file, mountDir));
            }
        }
        auto imported = keptImportedFiles(mountDir, going);
        std::erase_if(paths, [&](const std::filesystem::path& path) {
            for (const auto& file : bootFilesUnder(path)) {
                if (!imported) {
                    log::warn("cbs", std::format(L"{}: {} stays, imports not readable: {}", recipe.title, path.wstring(),
                                                 describe(imported.error())));
                    return true;
                }
                if (const auto found = imported->find(imageRelativePath(file, mountDir)); found != imported->end()) {
                    log::warn("cbs", std::format(L"{}: {} stays, a module that stays needs {} ({}, D-118)", recipe.title,
                                                 path.wstring(), file.filename().wstring(), found->second));
                    return true;
                }
            }
            return false;
        });
    }

    std::vector<CbsPackage> packages;
    if (!recipe.packages.empty() || !recipe.registry.empty()) {
        session.suspend();
        auto edited = editHives(mountDir, recipe);
        if (auto reopened = session.reload(); !reopened) {
            return reopened;
        }
        if (!edited) {
            return std::unexpected(edited.error());
        }
        packages = std::move(*edited);
    }
    if (!recipe.storeNames.empty() || !recipe.services.empty()) {
        // D-113: the installed copies and the services' keys (SYSTEM): DISM must let go of the hives.
        session.suspend();
        auto copies = removeStoreCopies(mountDir, recipe.storeNames, recipe.storeBits, recipe.services, task);
        if (auto reopened = session.reload(); !reopened) {
            return reopened;
        }
        if (!copies) {
            return std::unexpected(copies.error());
        }
    }
    if (!recipe.driverClasses.empty() || !recipe.driverPackages.empty()) {
        // Deep removal edits COMPONENTS / DRIVERS / SYSTEM and the store's files: DISM must let go.
        session.suspend();
        const auto deep = [&]() -> Result<void> {
            // D-112: named packages (one INF) or whole classes; the class limit is deepRemoveDrivers'.
            auto drivers = recipe.driverPackages.empty() ? findInboxDrivers(mountDir, recipe.driverClasses)
                                                         : findDriverPackages(mountDir, recipe.driverPackages);
            if (!drivers) {
                return std::unexpected(drivers.error());
            }
            if (drivers->empty()) {
                log::info("cbs", recipe.title + L": no inbox driver of these classes in the image");
                return {};
            }
            auto removedDrivers = deepRemoveDrivers(mountDir, *drivers, task);
            if (!removedDrivers) {
                return std::unexpected(removedDrivers.error());
            }
            return {};
        }();
        if (auto reopened = session.reload(); !reopened) {
            return reopened;
        }
        if (!deep) {
            return deep;
        }
    }
    task.report(0.05, recipe.title);

    // Apps (D-063): every provisioned version of each name; natively when DISM will not.
    if (!recipe.appx.empty()) {
        auto provisioned = session.appxPackages();
        if (!provisioned) {
            return std::unexpected(provisioned.error());
        }
        for (const auto& name : recipe.appx) {
            for (const auto& app : *provisioned) {
                if (_wcsicmp(app.displayName.c_str(), name.c_str()) != 0) {
                    continue;
                }
                auto removed = session.removeAppx(app.packageName);
                if (!removed && removed.error().hresult == kAppxRemovalRefused) {
                    log::info("cbs", L"DISM refuses this app; removing it natively: " + app.packageName);
                    removed = removeAppxNative(session, app.packageName, TaskContext{task.cancel, {}});
                }
                if (!removed) {
                    return removed;
                }
                log::info("cbs", L"app removed: " + app.packageName);
            }
        }
        // Listing the provisioned apps leaves the image's SOFTWARE hive loaded by DISM until the
        // session closes: every later registry step of the run then fails with 0x80070020
        // (measured: a recipe with "appx" + one HKLM value). A new session lets go of it.
        session.suspend();
        if (auto reopened = session.reload(); !reopened) {
            return reopened;
        }
    }

    // Packages: 5 % … 80 % of the step.
    std::size_t removed = 0;
    std::optional<Error> refused;
    for (std::size_t i = 0; i < packages.size(); ++i) {
        // Stopped by the user: a cancelled removal must not read as "DISM refused, delete the files".
        if (auto go = task.cancel.check(recipe.title); !go) {
            return go;
        }
        const auto& package = packages[i];
        const TaskContext part{task.cancel, [&](double fraction, std::wstring_view stage) {
                                   task.report(0.05 + 0.75 * (static_cast<double>(i) + std::clamp(fraction, 0.0, 1.0)) /
                                                          static_cast<double>(packages.size()),
                                               stage);
                               }};
        if (auto result = session.removePackage(package.identity, part); result) {
            ++removed;
            log::info("cbs", L"removed " + package.identity);
        } else if (stillInstalled(session, package.identity) == false) {
            // A child package goes with its parent: removing it afterwards finds nothing to do.
            ++removed;
            log::info("cbs", L"already gone with its parent: " + package.identity);
        } else if (package.state >= kCbsInstalled) {
            log::warn("cbs", L"package not removed: " + describe(result.error()));
            if (!refused) {
                refused = result.error();
            }
        } else {
            log::debug("cbs", L"staged leftover not removed: " + describe(result.error())); // an older version's payload
        }
        if (session.reloadRequired()) {
            if (auto reloaded = session.reload(); !reloaded) {
                return reloaded;
            }
        }
    }
    if (auto go = task.cancel.check(recipe.title); !go) {
        return go;
    }
    if (refused && paths.empty()) {
        return std::unexpected(*refused); // nothing else in the recipe takes the component out
    }
    task.report(0.8, recipe.title);

    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (auto gone = forceRemoveEntry(paths[i]); !gone) {
            return gone;
        }
        task.report(0.8 + 0.2 * static_cast<double>(i + 1) / static_cast<double>(paths.size()), recipe.title);
    }
    if (refused) {
        log::warn("cbs", std::format(L"{}: files removed, but {} of {} package(s) stayed in the component store",
                                     recipe.title, packages.size() - removed, packages.size()));
    }
    return {};
}

} // namespace wl::core
