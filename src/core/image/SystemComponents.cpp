#include "core/image/SystemComponents.h"

#include "base/Log.h"
#include "base/Text.h"
#include "base/Utf8.h"
#include "core/image/ComponentStore.h"
#include "core/image/DeepRemoval.h"
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
    if (!recipe.appx.empty()) {
        doc["appx"] = list(recipe.appx);
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
        for (const auto& item : doc.value("appx", Json::array())) {
            recipe.appx.push_back(utf8::toWide(item.get<std::string>()));
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

Result<void> validateComponentRecipe(const ComponentRecipe& recipe) {
    if (recipe.packages.empty() && recipe.paths.empty() && recipe.registry.empty() && recipe.driverClasses.empty() &&
        recipe.appx.empty()) {
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

Result<void> removeComponent(DismSession& session, const ComponentRecipe& recipe, const TaskContext& task) {
    if (auto valid = validateComponentRecipe(recipe); !valid) {
        return valid;
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
    if (!recipe.driverClasses.empty()) {
        // Deep removal edits COMPONENTS / DRIVERS / SYSTEM and the store's files: DISM must let go.
        session.suspend();
        const auto deep = [&]() -> Result<void> {
            auto drivers = findInboxDrivers(mountDir, recipe.driverClasses);
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
