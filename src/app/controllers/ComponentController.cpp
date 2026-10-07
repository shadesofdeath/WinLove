#include "app/controllers/ComponentController.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/ComponentStore.h"
#include "core/image/dism/StoreCleanup.h"
#include "core/image/dism/StoreShrink.h"

#include <algorithm>

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
    if (std::ranges::any_of(catalog.components(), [](const ComponentCatalogEntry& e) { return !e.recipe.packages.empty(); })) {
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

std::vector<ComponentController::Group> ComponentController::groups() const {
    const std::size_t appGroups = m_catalog.groups().size();
    std::vector<Group> groups(appGroups + m_systemCatalog.groups().size());
    for (std::size_t g = 0; g < groups.size(); ++g) {
        groups[g].catalogIndex = static_cast<int>(g);
        groups[g].name = g < appGroups ? m_catalog.groups()[g].name(m_language)
                                       : m_systemCatalog.groups()[g - appGroups].name.get(m_language);
    }
    const auto& list = m_state.appxList();
    if (!list || list->status != AppState::AppxList::Status::Ready) {
        return {};
    }
    const int other = m_catalog.groupIndex("other");
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
        const int g = item.entry ? m_catalog.groupIndex(item.entry->group) : other;
        auto& group = groups[static_cast<std::size_t>(g < 0 ? other : g)];
        group.size += item.size;
        group.items.push_back(std::move(item));
    }
    for (std::size_t g = 0; g < appGroups; ++g) {
        std::ranges::sort(groups[g].items,
                          [](const Item& a, const Item& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    }

    // System components this image has, in catalog order; the cleanup and the entries marked
    // "always" are on offer whatever the image has.
    const auto& system = m_state.systemComponents();
    const bool probed = system && system->mountDir == list->mountDir;
    for (const auto& entry : m_systemCatalog.components()) {
        const auto g = std::ranges::find(m_systemCatalog.groups(), entry.group, &ComponentCatalogGroup::id);
        if (g == m_systemCatalog.groups().end()) {
            continue;
        }
        Item item;
        item.system = &entry;
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
            if (!probed) {
                continue;
            }
            const auto found = system->items.find(entry.id);
            const bool present = found != system->items.end() && found->second.present;
            if (!present && !entry.always) {
                continue;
            }
            item.kind = Item::Kind::System;
            item.size = present ? found->second.size : 0;
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
        }
        auto& group = groups[appGroups + static_cast<std::size_t>(g - m_systemCatalog.groups().begin())];
        group.size += item.size;
        group.items.push_back(std::move(item));
    }
    std::erase_if(groups, [](const Group& g) { return g.items.empty(); });
    // Shown first: the system components and the cleanup are where the gigabytes are.
    std::ranges::stable_partition(groups, [appGroups](const Group& g) {
        return static_cast<std::size_t>(g.catalogIndex) >= appGroups;
    });
    return groups;
}

bool ComponentController::isComponentKind(OpKind kind) noexcept {
    return kind == OpKind::RemoveAppx || kind == OpKind::RemoveComponent || kind == OpKind::CleanupImage ||
           kind == OpKind::ShrinkStore;
}

Operation ComponentController::operationFor(const Item& item) {
    Operation op{kindOf(item), item.packageName};
    op.risk = item.risk;
    op.sizeDelta = -static_cast<std::int64_t>(item.size);
    if (item.kind == Item::Kind::System && item.system) {
        core::ComponentRecipe recipe = item.system->recipe;
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
    for (const auto& op : saved) {
        if (op.kind != OpKind::RemoveComponent) {
            continue;
        }
        const auto* entry = m_systemCatalog.find(utf8::fromWide(op.target));
        if (!entry || entry->kind != ComponentCatalogEntry::Kind::Remove) {
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

ComponentController::Check ComponentController::check(const Group& group) const {
    std::size_t n = 0;
    for (const auto& item : group.items) {
        n += queued(item) ? 1 : 0;
    }
    return n == 0 ? Check::Off : n >= group.items.size() ? Check::On : Check::Partial;
}

void ComponentController::toggle(const Item& item) {
    if (m_state.unqueue(kindOf(item), item.packageName)) {
        return;
    }
    queueItem(item);
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
        } else if (!queued(item)) {
            queueItem(item);
        }
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
