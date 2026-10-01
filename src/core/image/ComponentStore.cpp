#include "core/image/ComponentStore.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/OffReg.h"
#include "core/image/DeepRemoval.h"
#include "core/image/SystemComponents.h"
#include "core/system/BackupFiles.h"
#include "core/system/Privileges.h"

#include <pugixml.hpp>

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>

namespace wl::core {

namespace {

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

bool isHex(std::wstring_view s) {
    return !s.empty() && std::ranges::all_of(s, [](wchar_t c) { return std::iswxdigit(c) != 0; });
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

std::wstring ComponentStoreIndex::componentName(std::wstring_view key) {
    // name _ publicKeyToken(16 hex) _ version _ culture _ hash(hex)
    std::vector<std::size_t> cuts;
    for (std::size_t i = key.size(); i-- > 0;) {
        if (key[i] == L'_') {
            cuts.push_back(i);
            if (cuts.size() == 4) {
                break;
            }
        }
    }
    if (cuts.size() < 4) {
        return lower(key);
    }
    const std::wstring_view token = key.substr(cuts[3] + 1, cuts[2] - cuts[3] - 1);
    const std::wstring_view hash = key.substr(cuts[0] + 1);
    if (token.size() != 16 || !isHex(token) || !isHex(hash)) {
        return lower(key);
    }
    return lower(key.substr(0, cuts[3]));
}

std::wstring ComponentStoreIndex::deploymentPackage(const std::vector<std::uint8_t>& data) {
    if (data.size() < 8) {
        return {};
    }
    const std::uint32_t length = static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
                                 (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
    if (length == 0 || length > data.size() - 8) {
        return {};
    }
    std::wstring identity;
    identity.reserve(length);
    for (std::size_t i = 8; i < 8 + length; ++i) {
        if (data[i] < 0x20 || data[i] > 0x7e) {
            return {};
        }
        identity.push_back(static_cast<wchar_t>(data[i]));
    }
    return identity;
}

bool ComponentStoreIndex::isUpdatePackage(std::wstring_view family) {
    const std::wstring f = lower(family);
    if (!f.starts_with(L"package_")) {
        return false;
    }
    return f.starts_with(L"package_for_") || f.find(L"_for_") != std::wstring::npos;
}

std::vector<std::wstring> ComponentStoreIndex::manifestChildren(std::string_view xml) {
    std::vector<std::wstring> children;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        return children;
    }
    // <package> elements under <update> (any namespace prefix): each names a child package.
    for (const auto& node : doc.select_nodes("//*[local-name()='package']/*[local-name()='assemblyIdentity']")) {
        const char* name = node.node().attribute("name").value();
        if (*name) {
            children.push_back(lower(utf8::toWide(name)));
        }
    }
    return children;
}

void ComponentStoreIndex::addInstalled(std::wstring family) {
    m_installed.insert(lower(family));
}

void ComponentStoreIndex::addChild(std::wstring parent, std::wstring child) {
    m_children[lower(parent)].insert(lower(child));
}

void ComponentStoreIndex::addOwner(std::wstring component, std::wstring family) {
    const std::wstring c = lower(component);
    const std::wstring f = lower(family);
    if (m_owners[c].insert(f).second) {
        m_owned[f].push_back(c);
    }
}

void ComponentStoreIndex::addBytes(std::wstring component, std::uint64_t bytes) {
    m_bytes[lower(component)] += bytes;
}

std::vector<InboxDriver> ComponentStoreIndex::drivers(const std::vector<std::wstring>& classGuids) const {
    std::vector<InboxDriver> out;
    for (const auto& driver : m_drivers) {
        if (std::ranges::any_of(classGuids, [&](const std::wstring& g) { return normalizeClassGuid(g) == driver.classGuid; })) {
            out.push_back(driver);
        }
    }
    return out;
}

std::uint64_t ComponentStoreIndex::driverBytes(const std::vector<InboxDriver>& drivers) const {
    std::uint64_t total = 0;
    for (const auto& driver : drivers) {
        for (const wchar_t* arch : {L"amd64", L"wow64", L"x86", L"arm64"}) {
            if (const auto b = m_bytes.find(std::wstring(arch) + L"_dual_" + lower(driver.inf)); b != m_bytes.end()) {
                total += b->second;
            }
        }
    }
    return total;
}

void ComponentStoreIndex::addDriver(InboxDriver driver) {
    m_drivers.push_back(std::move(driver));
}

bool ComponentStoreIndex::installed(std::wstring_view family) const {
    return m_installed.contains(lower(family));
}

std::set<std::wstring> ComponentStoreIndex::tree(const std::vector<std::wstring>& families) const {
    std::set<std::wstring> seen;
    std::vector<std::wstring> stack;
    for (const auto& f : families) {
        stack.push_back(lower(f));
    }
    while (!stack.empty()) {
        std::wstring f = std::move(stack.back());
        stack.pop_back();
        if (!seen.insert(f).second) {
            continue;
        }
        if (const auto it = m_children.find(f); it != m_children.end()) {
            stack.insert(stack.end(), it->second.begin(), it->second.end());
        }
    }
    return seen;
}

std::uint64_t ComponentStoreIndex::exclusiveBytes(const std::vector<std::wstring>& families) const {
    const auto inside = tree(families);
    std::set<std::wstring> counted;
    std::uint64_t total = 0;
    for (const auto& family : inside) {
        const auto owned = m_owned.find(family);
        if (owned == m_owned.end()) {
            continue;
        }
        for (const auto& component : owned->second) {
            if (!counted.insert(component).second) {
                continue;
            }
            const auto& owners = m_owners.at(component);
            if (std::ranges::all_of(owners, [&](const std::wstring& o) { return inside.contains(o); })) {
                if (const auto b = m_bytes.find(component); b != m_bytes.end()) {
                    total += b->second;
                }
            }
        }
    }
    return total;
}

Result<ComponentStoreIndex> ComponentStoreIndex::build(const std::filesystem::path& mountDir, const TaskContext& task) {
    ComponentStoreIndex index;
    const auto config = mountDir / L"Windows" / L"System32" / L"config";

    // 1. Installed packages (SOFTWARE).
    {
        auto software = OffRegKey::openHive(config / L"SOFTWARE");
        if (!software) {
            return std::unexpected(software.error());
        }
        const auto packages = software->open(kCbsPackagesKey);
        for (const auto& identity : packages.subkeys()) {
            const auto state = packages.open(identity).dword(L"CurrentState");
            if (state && *state >= kCbsInstalled) {
                index.addInstalled(std::wstring(cbsPackageFamily(identity)));
            }
        }
    }
    task.report(0.1, L"CBS");
    if (auto go = task.cancel.check(); !go) {
        return std::unexpected(go.error());
    }

    // 2. Package tree (.mum of installed identities).
    (void)enablePrivilege(SE_BACKUP_NAME); // WinSxS and servicing are closed to administrators
    const auto servicing = mountDir / L"Windows" / L"servicing" / L"Packages";
    for (const auto& name : backupListFiles(servicing)) {
        const std::filesystem::path file = servicing / name;
        if (_wcsicmp(file.extension().c_str(), L".mum") != 0) {
            continue;
        }
        const std::wstring family(cbsPackageFamily(file.stem().wstring()));
        if (!index.installed(family)) {
            continue;
        }
        for (auto& child : manifestChildren(readFile(file))) {
            index.addChild(family, std::move(child));
        }
    }
    task.report(0.25, L"manifests");

    // 3. Ownership (COMPONENTS).
    {
        auto components = OffRegKey::openHive(config / L"COMPONENTS");
        if (!components) {
            return std::unexpected(components.error());
        }
        std::unordered_map<std::wstring, std::vector<std::wstring>> deploymentOwners;
        const auto deployments = components->open(L"CanonicalData\\Deployments");
        for (const auto& name : deployments.subkeys()) {
            std::vector<std::wstring> owners;
            for (const auto& value : deployments.open(name).values()) {
                if (value.name.starts_with(L"i!")) {
                    const std::wstring identity = deploymentPackage(value.data);
                    const std::wstring family = lower(cbsPackageFamily(identity));
                    if (!family.empty() && !isUpdatePackage(family)) {
                        owners.push_back(family);
                    }
                }
            }
            deploymentOwners.emplace(lower(name), std::move(owners));
        }
        task.report(0.4, L"deployments");
        if (auto go = task.cancel.check(); !go) {
            return std::unexpected(go.error());
        }
        const auto all = components->open(L"DerivedData\\Components");
        for (const auto& key : all.subkeys()) {
            const std::wstring name = componentName(key);
            for (const auto& value : all.open(key).values(/*withData=*/false)) {
                if (!value.name.starts_with(L"c!")) {
                    continue;
                }
                if (const auto it = deploymentOwners.find(lower(std::wstring_view(value.name).substr(2))); it != deploymentOwners.end()) {
                    for (const auto& owner : it->second) {
                        index.addOwner(name, owner);
                    }
                }
            }
        }
    }
    task.report(0.7, L"components");
    if (auto go = task.cancel.check(); !go) {
        return std::unexpected(go.error());
    }

    // 4. Payload bytes (WinSxS folders of owned components only). Listed through BackupFiles:
    // directory_iterator ends the whole listing at the first entry it cannot stat, and WinSxS of a
    // mounted image has such entries (2026-10-02: only folders before "amd64_microsoft-onecore-m…").
    const auto sxs = mountDir / L"Windows" / L"WinSxS";
    for (const auto& folder : backupListFolders(sxs)) {
        const std::wstring name = componentName(folder);
        if (index.m_owners.contains(name)) {
            index.addBytes(name, backupFolderSize(sxs / folder));
        }
    }
    task.report(0.95, L"WinSxS");

    // 5. Inbox drivers of the classes deep removal may take (D-060).
    if (auto drivers = findInboxDrivers(mountDir, {}); drivers) {
        index.m_drivers = std::move(*drivers);
    } else {
        log::warn("cbs", L"inbox drivers not read: " + describe(drivers.error()));
    }
    task.report(1.0, L"drivers");
    log::info("cbs", std::format(L"component store index: {} installed families, {} owned components", index.m_installed.size(),
                                 index.m_owners.size()));
    return index;
}

} // namespace wl::core
