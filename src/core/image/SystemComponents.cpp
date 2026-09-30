#include "core/image/SystemComponents.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/dism/Appx.h"
#include "core/system/FileLocks.h"
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

std::wstring lowered(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

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
        normalized += lowered(part);
    }
    if (parts.size() < 2) {
        return refuse(L"component path is too close to the image root");
    }
    if (std::ranges::find(kProtected, std::wstring_view(normalized)) != kProtected.end()) {
        return refuse(L"component path is a folder Windows needs");
    }
    return {};
}

bool isReparse(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

Error regError(LSTATUS status, const wchar_t* what, std::wstring detail) {
    return Error{status == ERROR_ACCESS_DENIED || status == ERROR_PRIVILEGE_NOT_HELD ? ErrorCode::AccessDenied
                                                                                   : ErrorCode::IoError,
                 what, std::move(detail), static_cast<std::int32_t>(HRESULT_FROM_WIN32(status))};
}

// Opens an existing key for writing. TrustedInstaller owns the servicing keys: on access denied
// the key is opened with backup / restore semantics (SeRestore), which ignores the ACL.
LSTATUS openForWrite(HKEY parent, const wchar_t* name, REGSAM access, HKEY& out) {
    LSTATUS status = RegOpenKeyExW(parent, name, 0, access, &out);
    if (status == ERROR_ACCESS_DENIED) {
        status = RegCreateKeyExW(parent, name, 0, nullptr, REG_OPTION_BACKUP_RESTORE, access, nullptr, &out, nullptr);
    }
    return status;
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
    if (recipe.packages.empty() && recipe.paths.empty() && recipe.registry.empty()) {
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
    }
    for (const auto& write : recipe.registry) {
        if (auto mapped = mapOfflineKey(write.key); !mapped) {
            return std::unexpected(mapped.error());
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
        if (i + 1 < parts.size() && isReparse(current)) {
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
            return f.size() == family.size() && _wcsnicmp(f.c_str(), family.data(), family.size()) == 0;
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
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD length = static_cast<DWORD>(std::size(name));
        const LSTATUS status = RegEnumKeyExW(packagesKey, i, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS) {
            continue;
        }
        HKEY raw = nullptr;
        if (RegOpenKeyExW(packagesKey, name, 0, KEY_QUERY_VALUE, &raw) != ERROR_SUCCESS) {
            continue;
        }
        const RegKey key(raw);
        packages.push_back({std::wstring(name, length), key.dword(L"Visibility").value_or(0),
                            key.dword(L"CurrentState").value_or(0)});
    }
    return packages;
}

Result<void> unlockCbsPackage(HKEY packagesKey, const std::wstring& identity) {
    HKEY raw = nullptr;
    if (const LSTATUS status = openForWrite(packagesKey, identity.c_str(), KEY_SET_VALUE | KEY_QUERY_VALUE, raw);
        status != ERROR_SUCCESS) {
        return std::unexpected(regError(status, L"could not open the package's servicing key", identity));
    }
    RegKey key(raw);
    if (key.dword(L"Visibility") != 1u) {
        const DWORD visible = 1;
        if (const LSTATUS status = RegSetValueExW(key.get(), L"Visibility", 0, REG_DWORD,
                                                  reinterpret_cast<const BYTE*>(&visible), sizeof(visible));
            status != ERROR_SUCCESS) {
            return std::unexpected(regError(status, L"could not make the package visible", identity));
        }
    }
    HKEY ownersRaw = nullptr;
    const LSTATUS opened = openForWrite(
        key.get(), L"Owners", DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE, ownersRaw);
    if (opened == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (opened != ERROR_SUCCESS) {
        return std::unexpected(regError(opened, L"could not open the package's Owners key", identity));
    }
    const RegKey owners(ownersRaw);
    if (const LSTATUS status = RegDeleteTreeW(owners.get(), nullptr); status != ERROR_SUCCESS) {
        return std::unexpected(regError(status, L"could not clear the package's Owners key", identity));
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

ComponentPresence probeComponent(const std::filesystem::path& mountDir, const ComponentRecipe& recipe) {
    ComponentPresence presence;
    (void)enablePrivilege(SE_BACKUP_NAME); // Program Files folders of an image can be closed to administrators
    for (const auto& relative : recipe.paths) {
        const auto path = resolveImagePath(mountDir, relative);
        if (!path) {
            continue;
        }
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path->c_str(), GetFileExInfoStandard, &data)) {
            continue;
        }
        presence.present = true;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue; // a link: removed as a link, nothing behind it is counted
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            presence.size += backupFolderSize(*path);
        } else {
            presence.size += (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
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
    task.report(0.05, recipe.title);

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
