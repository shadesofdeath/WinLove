#include "app/controllers/ComponentController.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "base/Text.h"
#include "core/image/ComponentStore.h"
#include "core/image/OffReg.h"
#include "core/image/DeepRemoval.h"
#include "core/image/ImageInventory.h"
#include "core/image/SystemComponents.h"
#include "core/image/dism/Appx.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/image/dism/StoreShrink.h"

#include <algorithm>
#include <array>

#include <map>
#include <set>
namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

namespace {

constexpr const wchar_t* kCleanupTarget = L"component-store";
constexpr const wchar_t* kCleanupCommand = L"DISM /Cleanup-Image /StartComponentCleanup";
constexpr const wchar_t* kShrinkTarget = L"component-store-shrink";
// What Ayarlar › Windows Update › "Otomatik güncelleştirmeler: Kapalı" writes: a shrunk store takes
// no update, and Windows would download one every day only to fail installing it.
constexpr const wchar_t* kNoAutoUpdateTarget = L"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU::NoAutoUpdate";
constexpr const wchar_t* kNoAutoUpdateValue = L"dword:00000001";

OpKind kindOf(const ComponentController::Item& item) {
    switch (item.kind) {
    case ComponentController::Item::Kind::System: return OpKind::RemoveComponent;
    case ComponentController::Item::Kind::Cleanup: return OpKind::CleanupImage;
    case ComponentController::Item::Kind::Shrink: return OpKind::ShrinkStore;
    case ComponentController::Item::Kind::Driver:
    case ComponentController::Item::Kind::Language:
    case ComponentController::Item::Kind::Keyboard:
    case ComponentController::Item::Kind::Font:
    case ComponentController::Item::Kind::SystemApp: return OpKind::RemoveComponent;
    case ComponentController::Item::Kind::Appx: break;
    }
    return OpKind::RemoveAppx;
}

} // namespace

ComponentController::ComponentController(AppState& state, AppxCatalog catalog, Language language,
                                         std::function<void(std::function<void()>)> postToUi,
                                         ComponentCatalog systemCatalog)
    : m_state(state), m_catalog(std::move(catalog)), m_systemCatalog(std::move(systemCatalog)), m_language(language),
      m_post(std::move(postToUi)) {
    m_subscription = m_state.subscribe([this](AppState::Change change) {
        if (change == AppState::Change::Components) {
            readSystem();
            readInventory();
        }
    });
}

ComponentController::~ComponentController() {
    *m_alive = false;
    m_state.unsubscribe(m_subscription);
}

void ComponentController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.appxList();
    if (!force && current && current->mountDir == mounted->mountDir &&
        current->status != AppState::AppxList::Status::Failed) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Loading, mountDir, {}, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<std::vector<core::AppxComponent>>(
        [mountDir](const core::TaskContext& task) -> Result<std::vector<core::AppxComponent>> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            return core::readAppx(**dism, mountDir, task);
        },
        [this, post, alive, mountDir](Result<std::vector<core::AppxComponent>> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                if (!m_state.mounted() || m_state.mounted()->mountDir != mountDir) {
                    return;
                }
                // Only the read that is still awaited: a list set meanwhile (a newer read) stays.
                if (const auto& now = m_state.appxList();
                    !now || now->mountDir != mountDir || now->status != AppState::AppxList::Status::Loading) {
                    return;
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    m_state.setAppxList(AppState::AppxList{AppState::AppxList::Status::Failed, mountDir, {}, result.error()});
                    return;
                }
                m_state.setAppxList(
                    AppState::AppxList{AppState::AppxList::Status::Ready, mountDir, std::move(*result), {}});
            });
        },
        {});
}

AppState::SystemComponents ComponentController::probeSystem(const ComponentCatalog& catalog,
                                                            const std::filesystem::path& mountDir) {
    AppState::SystemComponents found{mountDir, {}};
    // Package-level entries (D-059) need the image's component store: read once for all of them.
    std::optional<core::ComponentStoreIndex> store;
    if (std::ranges::any_of(catalog.components(), [](const ComponentCatalogEntry& e) {
            return !e.recipe.packages.empty() || !e.store.empty();
        })) {
        core::CancelToken never;
        if (auto built = core::ComponentStoreIndex::build(mountDir, core::TaskContext{never, {}})) {
            store = std::move(*built);
        } else {
            log::warn("app", L"component store not read, package sizes unknown: " + describe(built.error()));
        }
    }
    for (const auto& entry : catalog.components()) {
        if (entry.kind == ComponentCatalogEntry::Kind::Remove) {
            found.items[entry.id] = core::probeComponent(mountDir, entry.recipe, store ? &*store : nullptr);
        } else if (entry.kind == ComponentCatalogEntry::Kind::Shrink) {
            // What shrinking would free, measured (~5 s): only what no other folder links to.
            core::CancelToken never;
            if (auto plan = core::planStoreShrink(mountDir, /*measure=*/true, core::TaskContext{never, {}})) {
                found.items[entry.id] = {true, plan->freed};
            }
        }
    }
    // D-113: an entry with services is there when one of them is (the image's SYSTEM hive, read only).
    if (std::ranges::any_of(catalog.components(), [](const ComponentCatalogEntry& e) { return !e.recipe.services.empty(); })) {
        std::set<std::wstring> services;
        if (auto hive = core::OffRegKey::openHive(mountDir / L"Windows" / L"System32" / L"config" / L"SYSTEM")) {
            for (const auto& name : hive->open(L"ControlSet001\\Services").subkeys()) {
                services.insert(wl::text::fold(name));
            }
        }
        for (const auto& entry : catalog.components()) {
            if (std::ranges::any_of(entry.recipe.services, [&](const std::wstring& s) { return services.contains(wl::text::fold(s)); })) {
                found.items[entry.id].present = true;
            }
        }
    }
    // D-113: the store split between the definitions (each component to its most specific pattern).
    if (store) {
        std::vector<std::vector<std::wstring>> patterns;
        std::vector<const ComponentCatalogEntry*> owners;
        for (const auto& entry : catalog.components()) {
            if (entry.kind == ComponentCatalogEntry::Kind::Remove && !entry.store.empty()) {
                patterns.push_back(entry.store);
                owners.push_back(&entry);
            }
        }
        auto shares = store->shareStore(patterns);
        for (std::size_t i = 0; i < owners.size(); ++i) {
            if (!shares[i].empty()) {
                found.store[owners[i]->id] = std::move(shares[i]);
            }
        }
    }
    return found;
}

void ComponentController::readSystem() {
    const auto& mounted = m_state.mounted();
    const auto& apps = m_state.appxList();
    if (!mounted || !apps || apps->status != AppState::AppxList::Status::Ready || apps->mountDir != mounted->mountDir) {
        return;
    }
    const auto& known = m_state.systemComponents();
    const bool any = std::ranges::any_of(m_systemCatalog.components(), [](const ComponentCatalogEntry& e) {
        return e.kind == ComponentCatalogEntry::Kind::Remove;
    });
    if (!any || (known && known->mountDir == mounted->mountDir) || m_probing == mounted->mountDir) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_probing = mountDir;
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<AppState::SystemComponents>(
        // A copy of the catalog: the job may outlive the controller.
        [catalog = m_systemCatalog, mountDir](const core::TaskContext&) -> Result<AppState::SystemComponents> {
            return probeSystem(catalog, mountDir);
        },
        [this, post, alive, mountDir](Result<AppState::SystemComponents> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_probing.clear();
                if (result && m_state.mounted() && m_state.mounted()->mountDir == mountDir) {
                    m_state.setSystemComponents(std::move(*result));
                }
            });
        },
        {});
}

void ComponentController::readInventory() {
    const auto& mounted = m_state.mounted();
    const auto& apps = m_state.appxList();
    if (!mounted || !apps || apps->status != AppState::AppxList::Status::Ready || apps->mountDir != mounted->mountDir) {
        return;
    }
    const auto& known = m_state.imageInventory();
    if ((known && known->mountDir == mounted->mountDir) || m_inventoryReading == mounted->mountDir) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_inventoryReading = mountDir;
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<AppState::ImageInventory>(
        [mountDir](const core::TaskContext&) -> Result<AppState::ImageInventory> {
            auto data = core::readImageInventory(mountDir);
            if (!data) {
                return std::unexpected(data.error());
            }
            return AppState::ImageInventory{mountDir, std::move(*data)};
        },
        [this, post, alive, mountDir](Result<AppState::ImageInventory> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_inventoryReading.clear();
                if (!result) {
                    log::warn("components", L"image inventory: " + result.error().message);
                    return;
                }
                if (m_state.mounted() && m_state.mounted()->mountDir == mountDir) {
                    m_state.setImageInventory(std::move(*result));
                }
            });
        },
        {});
}

namespace {

bool sameText(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

// Network services, protocols and clients: the reference tool keeps them with the class INFs under
// "Donanım desteği › Tek tek cihazlar [Gelişmiş]" (README §6.4).
bool advancedClass(std::wstring_view guid) {
    return sameText(guid, L"{4D36E974-E325-11CE-BFC1-08002BE10318}") || sameText(guid, L"{4D36E975-E325-11CE-BFC1-08002BE10318}") ||
           sameText(guid, L"{4D36E973-E325-11CE-BFC1-08002BE10318}");
}

// Fonts held for good: core::isCoreFontFamily (D-119; the reference tool's "Temel işlevsellik" fonts, README §6.7,
// the shell's icon fonts and the families Windows' web engines fall back to).

// System apps a Windows install is fine without (the rest are the shell, sign-in, setup, the
// lock screen …: held). Identity names.
constexpr std::array<std::wstring_view, 18> kRemovableSystemApps = {
    L"Microsoft.Windows.PeopleExperienceHost", L"Microsoft.Windows.ParentalControls", L"Microsoft.XboxGameCallableUI",
    L"Microsoft.BioEnrollment", L"Microsoft.Windows.NarratorQuickStart", L"Microsoft.Windows.PinningConfirmationDialog",
    L"Microsoft.Windows.CapturePicker", L"Microsoft.Windows.AssignedAccessLockApp", L"Microsoft.Windows.PrintQueueActionCenter",
    L"Microsoft.Windows.XGpuEjectDialog", L"Microsoft.ECApp", L"Microsoft.Windows.ContentDeliveryManager",
    L"Microsoft.MicrosoftEdgeDevToolsClient", L"Microsoft.Windows.SecureAssessmentBrowser", L"Microsoft.HEIFImageExtension",
    L"Microsoft.VP9VideoExtensions", L"Microsoft.WebpImageExtension", L"Microsoft.RawImageExtension"};

} // namespace

void ComponentController::addInventory(std::vector<Item>& items) const {
    const auto& inventory = m_state.imageInventory();
    const auto& mounted = m_state.mounted();
    if (!inventory || !mounted || inventory->mountDir != mounted->mountDir) {
        return;
    }
    auto node = [this](const char* id) { return m_systemCatalog.nodeName(id, m_language); };
    // The edition's default language (its own folders, keyboards stay).
    std::wstring language;
    if (const auto& source = m_state.source()) {
        for (const auto& image : source->install.images) {
            if (image.index == mounted->index) {
                language = image.defaultLanguage;
            }
        }
    }
    wchar_t lcid[16] = {};
    if (const LCID id = language.empty() ? 0 : LocaleNameToLCID(language.c_str(), 0); id != 0) {
        swprintf_s(lcid, L"%04X", static_cast<unsigned>(LANGIDFROMLCID(id)));
    }
    const auto& data = inventory->data;
    for (const auto& d : data.drivers) {
        Item item;
        item.kind = Item::Kind::Driver;
        item.name = (d.provider.empty() ? std::wstring() : d.provider + L"  |  ") + d.inf;
        item.packageName = L"driver:" + d.package;
        item.identity = d.package;
        item.detail = d.version + (d.date.empty() ? L"" : L" " + d.date);
        item.size = d.size;
        item.children = d.devices;
        item.risk = core::ops::Risk::High;
        const bool advanced = d.classInf || advancedClass(d.classGuid);
        item.category = advanced ? "hardware" : "drivers";
        item.path = advanced ? std::vector<std::wstring>{node("advancedDevices"), d.className} : std::vector<std::wstring>{d.className};
        // Deep removal takes legacy classes only (D-060, the user's rule: never network, storage, USB …).
        if (d.classInf) {
            item.lockReason = node("lockClassInf");
        } else if (!core::isDeepRemovableClass(d.classGuid)) {
            item.lockReason = node("lockClass");
        }
        item.recipe = core::driverRecipe(d);
        item.recipe.title = item.name;
        items.push_back(std::move(item));
    }
    for (const auto& l : data.languages) {
        Item item;
        item.kind = Item::Kind::Language;
        item.name = l.name;
        item.packageName = L"language:" + l.tag;
        item.identity = l.tag;
        item.detail = l.tag;
        item.size = l.size;
        item.risk = core::ops::Risk::Medium;
        item.category = "localization";
        item.path = {node("languages")};
        std::wstring tags = l.tag + L",";
        for (const std::wstring& keep : {language, std::wstring(L"en-US")}) {
            if (!keep.empty()) {
                std::wstring_view rest = tags;
                while (!rest.empty()) {
                    const auto comma = rest.find(L',');
                    if (sameText(rest.substr(0, comma), keep)) {
                        item.lockReason = node("lockLanguage");
                    }
                    rest = comma == std::wstring_view::npos ? std::wstring_view() : rest.substr(comma + 1);
                }
            }
        }
        if (l.paths.empty()) {
            item.lockReason = node("lockLanguage");
        }
        item.recipe = core::languageRecipe(l);
        items.push_back(std::move(item));
    }
    for (const auto& k : data.keyboards) {
        Item item;
        item.kind = Item::Kind::Keyboard;
        item.name = k.name;
        item.packageName = L"keyboard:" + k.klid;
        item.identity = k.klid;
        item.detail = k.klid + L" \u00b7 " + k.file;
        item.risk = core::ops::Risk::Medium;
        item.category = "localization";
        item.path = {node("keyboards")};
        const std::wstring_view low = k.klid.size() >= 4 ? std::wstring_view(k.klid).substr(k.klid.size() - 4) : std::wstring_view();
        if (sameText(k.klid, L"00000409") || (lcid[0] != 0 && sameText(low, lcid))) {
            item.lockReason = node("lockBasic");
        }
        item.recipe = core::keyboardRecipe(k);
        items.push_back(std::move(item));
    }
    for (const auto& f : data.fonts) {
        Item item;
        item.kind = Item::Kind::Font;
        item.name = f.family;
        item.packageName = L"font:" + f.family;
        item.identity = f.files.empty() ? f.family : f.files.front();
        item.size = f.size;
        item.risk = core::ops::Risk::Medium;
        item.category = "localization";
        item.path = {node("fonts")};
        if (core::isCoreFontFamily(f.family) || f.files.empty()) { // D-119
            item.lockReason = node("lockBasic");
        }
        item.recipe = core::fontRecipe(f);
        items.push_back(std::move(item));
    }
    for (const auto& a : data.systemApps) {
        Item item;
        item.kind = Item::Kind::SystemApp;
        const AppxCatalogEntry* known = m_catalog.find(a.name);
        item.entry = known;
        item.name = known ? known->name(m_language) : !a.displayName.empty() ? a.displayName : a.name;
        item.packageName = L"systemapp:" + a.family;
        item.identity = a.name;
        item.detail = a.folder;
        item.size = a.size;
        item.risk = core::ops::Risk::High;
        // Most system apps are shell experiences: the ones the catalog does not know go under Kabuk ve Masaüstü.
        item.category = known ? known->category : std::string("shell");
        item.tier = known ? known->tier : Tier::None;
        if (std::ranges::none_of(kRemovableSystemApps, [&](std::wstring_view name) { return sameText(name, a.name); })) {
            item.lockReason = node("lockSystemApp");
        }
        item.recipe = core::systemAppRecipe(a);
        item.recipe.title = item.name;
        items.push_back(std::move(item));
    }
}

std::vector<ComponentController::Item> ComponentController::allItems() const {
    const auto& list = m_state.appxList();
    if (!list || list->status != AppState::AppxList::Status::Ready) {
        return {};
    }
    std::vector<Item> items;
    std::vector<Item> pending; // D-113: rows a catalog entry adds after itself (its 32-bit part)
    addInventory(items);
    // The mounted edition's build: some apps are protected from a certain Windows build on.
    for (std::size_t i = 0; i < list->items.size(); ++i) {
        const auto& appx = list->items[i];
        Item item;
        item.source = static_cast<int>(i);
        item.entry = m_catalog.find(appx.package.displayName);
        item.name = item.entry ? item.entry->name(m_language) : appx.package.displayName;
        item.risk = item.entry ? item.entry->risk : core::ops::Risk::Medium;
        item.size = appx.size;
        item.packageName = appx.package.packageName;
        item.identity = appx.package.displayName;
        item.notes = item.entry ? item.entry->notes(m_language) : std::wstring();
        item.contents = {appx.package.packageName};
        item.category = item.entry ? item.entry->category : std::string("uncategorized");
        item.tier = item.entry ? item.entry->tier : Tier::None;
        items.push_back(std::move(item));
    }

    // System components this image has, in catalog order; the cleanup and the entries marked
    // "always" are on offer whatever the image has.
    const auto& system = m_state.systemComponents();
    const bool probed = system && system->mountDir == list->mountDir;
    for (const auto& entry : m_systemCatalog.components()) {
        Item item;
        item.system = &entry;
        item.category = entry.category;
        item.tier = entry.tier;
        item.name = entry.name.get(m_language);
        item.risk = entry.risk;
        item.notes = entry.notes.get(m_language);
        item.deep = entry.deep;
        item.packageName = utf8::toWide(entry.id);
        if (entry.kind == ComponentCatalogEntry::Kind::Cleanup) {
            item.kind = Item::Kind::Cleanup;
            item.packageName = kCleanupTarget;
            item.identity = kCleanupCommand;
            item.contents = {std::wstring(kCleanupCommand) + (entry.resetBase ? L" /ResetBase" : L"")};
        } else if (entry.kind == ComponentCatalogEntry::Kind::Shrink) {
            item.kind = Item::Kind::Shrink;
            item.packageName = kShrinkTarget;
            item.identity = L"Windows\\WinSxS";
            if (probed) {
                if (const auto found = system->items.find(entry.id); found != system->items.end()) {
                    item.size = found->second.size;
                }
            }
            item.contents = {L"WinSxS: Manifests, Catalogs, FileMaps, Fusion, InstallTemp, SettingsManifests, Temp",
                             L"common-controls, gdiplus, isolationautomation, vc80.crt, vc90.crt",
                             L"servicingstack", L"WindowsUpdate\\AU : NoAutoUpdate = 1"};
        } else {
            if (!probed || entry.container) {
                continue; // D-113: a container is a node over what is under it (below)
            }
            const auto found = system->items.find(entry.id);
            const auto share = system->store.find(entry.id);
            const bool stored = share != system->store.end() && !share->second.empty();
            const bool present = (found != system->items.end() && found->second.present) || stored;
            if (!present && !entry.always) {
                continue;
            }
            item.kind = Item::Kind::System;
            item.size = found != system->items.end() && found->second.present ? found->second.size : 0;
            item.recipe = entry.recipe;
            if (entry.locked) {
                item.lockReason = m_systemCatalog.nodeName("lockRequired", m_language);
            }
            if (stored) {
                // D-113: its own part of the store; the 32-bit part is a row of its own under it.
                const core::StoreShare& part = share->second;
                const bool split = part.bytes32 > 0 && !part.names64.empty();
                item.size += part.bytes64 + (split ? 0 : part.bytes32);
                item.recipe.storeNames = part.names64;
                if (!split) {
                    item.recipe.storeNames.insert(item.recipe.storeNames.end(), part.names32.begin(), part.names32.end());
                    std::ranges::sort(item.recipe.storeNames);
                    item.recipe.storeNames.erase(std::ranges::unique(item.recipe.storeNames).begin(), item.recipe.storeNames.end());
                }
                // D-120: what Windows needs is not part of the removal, whichever pattern reaches it.
                std::erase_if(item.recipe.storeNames, [](const std::wstring& n) { return core::isProtectedStoreName(n); });
                item.recipe.storeBits = split ? 64 : 0;
                item.recipe.afterUpdates = true; // a later update in the same run would bring the files back
                if (split) {
                    Item bits32 = item;
                    bits32.name = item.name + m_systemCatalog.nodeName("bits32", m_language);
                    bits32.packageName = item.packageName + L":32";
                    bits32.parentKey = item.packageName;
                    bits32.size = part.bytes32;
                    bits32.recipe = {};
                    bits32.recipe.storeNames = part.names32;
                    std::erase_if(bits32.recipe.storeNames, [](const std::wstring& n) { return core::isProtectedStoreName(n); });
                    bits32.recipe.storeBits = 32;
                    bits32.recipe.afterUpdates = true;
                    bits32.identity = part.names32.front();
                    bits32.contents = part.names32;
                    if (!bits32.recipe.storeNames.empty()) {
                        pending.push_back(std::move(bits32));
                    }
                }
            }
            item.identity = !entry.recipe.paths.empty()      ? entry.recipe.paths.front()
                            : !entry.recipe.packages.empty() ? entry.recipe.packages.front()
                                                             : utf8::toWide(entry.id);
            item.contents = entry.recipe.packages;
            item.contents.insert(item.contents.end(), entry.recipe.driverClasses.begin(), entry.recipe.driverClasses.end());
            item.contents.insert(item.contents.end(), entry.recipe.paths.begin(), entry.recipe.paths.end());
            item.contents.insert(item.contents.end(), entry.recipe.appx.begin(), entry.recipe.appx.end());
            if (entry.always) { // mostly registry: what it changes is what there is to show
                for (const auto& write : entry.recipe.registry) {
                    item.contents.push_back(write.name.empty() ? write.key : write.key + L" : " + write.name);
                }
            }
            if (stored) {
                const auto& names = item.recipe.storeNames;
                if (item.identity == utf8::toWide(entry.id) && !names.empty()) {
                    item.identity = names.front();
                }
                item.contents.insert(item.contents.end(), names.begin(), names.end());
                item.contents.insert(item.contents.end(), entry.recipe.services.begin(), entry.recipe.services.end());
            }
        }
        items.push_back(std::move(item));
        for (auto& extra : pending) {
            items.push_back(std::move(extra));
        }
        pending.clear();
    }
    placeDependents(items);
    return items;
}

// D-113: each catalog item under the nearest present entry it depends on (parentKey, same category
// and nodes as that one); container entries above it become its nodes.
void ComponentController::placeDependents(std::vector<Item>& items) const {
    std::map<std::wstring, std::size_t> at;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::Kind::System && items[i].parentKey.empty()) {
            at.emplace(items[i].packageName, i);
        }
    }
    std::vector<std::size_t> order; // parents before children: by depth in the catalog
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::Kind::System && items[i].system && items[i].parentKey.empty()) {
            order.push_back(i);
        }
    }
    const auto depth = [&](const ComponentCatalogEntry* e) {
        int d = 0;
        for (const auto* up = e; up && !up->parent.empty() && d < 64; up = m_systemCatalog.find(up->parent)) {
            ++d;
        }
        return d;
    };
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return depth(items[a].system) < depth(items[b].system); });
    for (const std::size_t i : order) {
        Item& item = items[i];
        std::vector<std::wstring> containers;
        for (const auto* up = m_systemCatalog.find(item.system->parent); up; up = m_systemCatalog.find(up->parent)) {
            if (up->container) {
                containers.push_back(up->name.get(m_language));
                continue;
            }
            if (const auto parent = at.find(utf8::toWide(up->id)); parent != at.end()) {
                const Item& p = items[parent->second];
                item.parentKey = p.packageName;
                item.category = p.category;
                item.path = p.path;
                containers.clear();
                break;
            }
        }
        if (item.parentKey.empty() && !containers.empty()) {
            item.path.assign(containers.rbegin(), containers.rend());
        }
    }
    // The 32-bit rows follow their own row's place.
    for (auto& item : items) {
        if (item.kind == Item::Kind::System && item.packageName.ends_with(L":32")) {
            if (const auto parent = at.find(item.parentKey); parent != at.end()) {
                item.category = items[parent->second].category;
                item.path = items[parent->second].path;
            }
        }
    }
}

std::vector<ComponentController::Group> ComponentController::groups() const {
    // D-110: the reference tool's tree — its categories in their order; an item whose category the catalog
    // does not name goes under "uncategorized".
    const auto& categories = m_systemCatalog.categories();
    std::vector<Group> groups(categories.size());
    std::size_t uncategorized = categories.size();
    for (std::size_t g = 0; g < categories.size(); ++g) {
        groups[g].catalogIndex = static_cast<int>(g);
        groups[g].name = categories[g].name.get(m_language);
        if (categories[g].id == "uncategorized") {
            uncategorized = g;
        }
    }
    if (uncategorized == categories.size()) {
        groups.push_back({static_cast<int>(categories.size()), m_language == Language::Turkish ? L"Kategorisiz" : L"Uncategorized", {}, 0});
    }
    for (auto& item : allItems()) {
        if (item.kind == Item::Kind::Cleanup || item.kind == Item::Kind::Shrink) {
            continue;
        }
        std::size_t g = uncategorized;
        for (std::size_t i = 0; i < categories.size(); ++i) {
            if (categories[i].id == item.category) {
                g = i;
            }
        }
        groups[g].size += item.size;
        groups[g].items.push_back(std::move(item));
    }
    for (auto& group : groups) {
        std::ranges::sort(group.items, [](const Item& a, const Item& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    }
    std::erase_if(groups, [](const Group& g) { return g.items.empty(); });
    return groups;
}

std::vector<ComponentController::Item> ComponentController::modeItems() const {
    std::vector<Item> items = allItems();
    std::erase_if(items, [](const Item& item) { return item.kind != Item::Kind::Cleanup && item.kind != Item::Kind::Shrink; });
    return items;
}

ComponentController::Level ComponentController::level() const {
    if (m_state.changes().find(OpKind::ShrinkStore, kShrinkTarget)) {
        return Level::Maximum;
    }
    return m_state.changes().find(OpKind::CleanupImage, kCleanupTarget) ? Level::Deep : Level::DefaultPlus;
}

void ComponentController::setLevel(Level wanted) {
    // Derin: the store cleanup; Maksimum: the cleanup and the shrink; Varsayılan+: neither.
    for (const auto& item : modeItems()) {
        const bool want = item.kind == Item::Kind::Cleanup ? wanted != Level::DefaultPlus : wanted == Level::Maximum;
        if (want && !queued(item)) {
            queueItem(item);
        } else if (!want && queued(item)) {
            m_state.unqueue(kindOf(item), item.packageName);
        }
    }
}

int ComponentController::applyTemplate(Tier chosen) {
    std::vector<Operation> ops;
    for (const auto& group : groups()) {
        for (const auto& item : group.items) {
            if (templateTakes(chosen, item.tier) && !queued(item) && block(item).empty()) {
                ops.push_back(operationFor(item));
            }
        }
    }
    const int n = static_cast<int>(ops.size());
    if (n > 0) {
        m_state.queueMany(std::move(ops));
    }
    return n;
}

bool ComponentController::isComponentKind(OpKind kind) noexcept {
    return kind == OpKind::RemoveAppx || kind == OpKind::RemoveComponent || kind == OpKind::CleanupImage ||
           kind == OpKind::ShrinkStore;
}

Operation ComponentController::operationFor(const Item& item) {
    Operation op{kindOf(item), item.packageName};
    op.risk = item.risk;
    op.sizeDelta = -static_cast<std::int64_t>(item.size);
    if (item.kind == Item::Kind::Driver || item.kind == Item::Kind::Language || item.kind == Item::Kind::Keyboard ||
        item.kind == Item::Kind::Font || item.kind == Item::Kind::SystemApp) {
        core::ComponentRecipe recipe = item.recipe;
        recipe.title = item.name;
        op.value = utf8::toWide(core::componentRecipeToJson(recipe));
    } else if (item.kind == Item::Kind::System) {
        core::ComponentRecipe recipe = item.recipe; // the catalog's, D-113: with this image's store names
        recipe.title = item.name;
        op.value = utf8::toWide(core::componentRecipeToJson(recipe));
    } else if (item.kind == Item::Kind::Cleanup) {
        op.value = utf8::toWide(core::storeCleanupToJson({item.name, !item.system || item.system->resetBase}));
    } else if (item.kind == Item::Kind::Shrink) {
        op.value = utf8::toWide(core::storeShrinkToJson({item.name}));
    } else {
        op.value = item.name; // not used by the engine: the name the Apply lists show instead of the package id
    }
    return op;
}

core::ops::ChangeSet ComponentController::withCurrentRecipes(core::ops::ChangeSet changes) const {
    const std::vector<Operation> saved = changes.operations();
    // Apps by their full name, version included: a preset from another build names another
    // version. With this image's list at hand the queue gets this image's name for the app, so
    // the Components page shows it as queued (audit A4; the Applier also finds it by family).
    const auto& list = m_state.appxList();
    if (list && list->status == AppState::AppxList::Status::Ready) {
        for (const auto& op : saved) {
            if (op.kind != OpKind::RemoveAppx) {
                continue;
            }
            const std::wstring family = core::appxFamilyName(op.target);
            const bool asNamed = std::ranges::any_of(list->items, [&](const core::AppxComponent& app) {
                return _wcsicmp(app.package.packageName.c_str(), op.target.c_str()) == 0;
            });
            if (family.empty() || asNamed) {
                continue; // not a package name, or this image has it by this very name
            }
            for (const auto& app : list->items) {
                if (_wcsicmp(core::appxFamilyName(app.package.packageName).c_str(), family.c_str()) == 0) {
                    Operation mapped = op;
                    mapped.target = app.package.packageName;
                    changes.remove(op.kind, op.target);
                    changes.add(std::move(mapped));
                    break;
                }
            }
        }
    }
    for (const auto& op : saved) {
        if (op.kind != OpKind::RemoveComponent) {
            continue;
        }
        std::wstring id = op.target;
        if (id.ends_with(L":32")) {
            id.resize(id.size() - 3);
        }
        const auto* entry = m_systemCatalog.find(utf8::fromWide(id));
        if (!entry || entry->kind != ComponentCatalogEntry::Kind::Remove) {
            continue;
        }
        if (!entry->store.empty()) {
            // D-113: the names are this image's: the current row's recipe, when the image has it.
            for (const auto& item : allItems()) {
                if (item.kind == Item::Kind::System && item.packageName == op.target) {
                    Operation current = operationFor(item);
                    changes.add(std::move(current));
                    break;
                }
            }
            continue;
        }
        Operation current = op;
        core::ComponentRecipe recipe = entry->recipe;
        recipe.title = entry->name.get(m_language);
        current.value = utf8::toWide(core::componentRecipeToJson(recipe));
        current.risk = entry->risk;
        changes.add(std::move(current));
    }
    return changes;
}

bool ComponentController::queued(const Item& item) const {
    return m_state.changes().find(kindOf(item), item.packageName) != nullptr;
}

core::ops::CompatBlock ComponentController::block(const Item& item) const {
    if (!item.lockReason.empty()) {
        return {{item.lockReason}, {}}; // D-112: held for good (a required driver class, the default language …)
    }
    if (!blockOf || queued(item)) {
        return {};
    }
    return blockOf(operationFor(item));
}

ComponentController::Check ComponentController::check(const Group& group) const {
    // Items a guard holds do not count: a group whose other items are all queued is "on".
    std::size_t n = 0;
    std::size_t open = 0;
    for (const auto& item : group.items) {
        const bool q = queued(item);
        n += q ? 1 : 0;
        open += q || block(item).empty() ? 1 : 0;
    }
    return n == 0 ? Check::Off : n >= open ? Check::On : Check::Partial;
}

void ComponentController::toggle(const Item& item) {
    if (queued(item)) {
        // Kept again: what it depends on comes back with it (D-113).
        std::vector<Item> back = dependencies(item);
        back.push_back(item);
        setRemoved(back, false);
        return;
    }
    if (const auto held = block(item); !held.empty()) {
        if (onBlocked) {
            onBlocked(item, held);
        }
        return;
    }
    std::vector<Item> gone = dependents(item);
    if (gone.empty()) {
        queueItem(item);
        return;
    }
    gone.insert(gone.begin(), item);
    setRemoved(gone, true); // what depends on it goes too; held ones stay where they are
}

std::vector<ComponentController::Item> ComponentController::dependents(const Item& item) const {
    const auto all = allItems();
    std::vector<Item> out;
    std::set<std::wstring> keys{item.packageName};
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& other : all) {
            if (!other.parentKey.empty() && keys.contains(other.parentKey) && !keys.contains(other.packageName)) {
                keys.insert(other.packageName);
                out.push_back(other);
                grew = true;
            }
        }
    }
    return out;
}

std::vector<ComponentController::Item> ComponentController::dependencies(const Item& item) const {
    const auto all = allItems();
    std::vector<Item> out;
    std::wstring key = item.parentKey;
    for (int guard = 0; !key.empty() && guard < 64; ++guard) {
        const auto up = std::ranges::find(all, key, &Item::packageName);
        if (up == all.end()) {
            break;
        }
        out.push_back(*up);
        key = up->parentKey;
    }
    return out;
}

// The shrink takes automatic updates off with it (one queue edit); taken out of the queue, it
// leaves that setting where it is — Ayarlar shows it and can turn it back.
void ComponentController::queueItem(const Item& item) {
    if (item.kind != Item::Kind::Shrink || m_state.changes().find(OpKind::SetRegistryValue, kNoAutoUpdateTarget)) {
        m_state.queue(operationFor(item));
        return;
    }
    Operation updates{OpKind::SetRegistryValue, kNoAutoUpdateTarget, kNoAutoUpdateValue};
    updates.risk = core::ops::Risk::Medium;
    m_state.queueMany({operationFor(item), std::move(updates)});
}

void ComponentController::toggleGroup(const Group& group) {
    const bool all = check(group) == Check::On;
    for (const auto& item : group.items) {
        if (all) {
            m_state.unqueue(kindOf(item), item.packageName);
        } else if (!queued(item) && block(item).empty()) {
            queueItem(item);
        }
    }
}

void ComponentController::setRemoved(const std::vector<Item>& items, bool removed) {
    std::vector<Operation> add;
    std::vector<std::pair<OpKind, std::wstring>> drop;
    for (const auto& item : items) {
        const bool q = queued(item);
        if (removed && !q && block(item).empty()) {
            add.push_back(operationFor(item));
        } else if (!removed && q) {
            drop.emplace_back(kindOf(item), item.packageName);
        }
    }
    if (!add.empty()) {
        m_state.queueMany(std::move(add));
    }
    if (!drop.empty()) {
        m_state.unqueueMany(drop);
    }
}

void ComponentController::resetChanges() {
    m_state.unqueueIf([](const Operation& op) { return isComponentKind(op.kind); });
}

std::size_t ComponentController::queuedCount() const {
    return m_state.changes().count(OpKind::RemoveAppx) + m_state.changes().count(OpKind::RemoveComponent) +
           m_state.changes().count(OpKind::CleanupImage) + m_state.changes().count(OpKind::ShrinkStore);
}

std::uint64_t ComponentController::queuedBytes() const {
    std::uint64_t total = 0;
    for (const auto& op : m_state.changes().operations()) {
        if (isComponentKind(op.kind) && op.sizeDelta < 0) {
            total += static_cast<std::uint64_t>(-op.sizeDelta);
        }
    }
    return total;
}

} // namespace wl::app
