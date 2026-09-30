#include "app/controllers/ComponentController.h"

#include "base/Log.h"
#include "base/Utf8.h"
#include "core/image/dism/StoreCleanup.h"

#include <algorithm>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

namespace {

constexpr const wchar_t* kCleanupTarget = L"component-store";
constexpr const wchar_t* kCleanupCommand = L"DISM /Cleanup-Image /StartComponentCleanup";

OpKind kindOf(const ComponentController::Item& item) {
    switch (item.kind) {
    case ComponentController::Item::Kind::System: return OpKind::RemoveComponent;
    case ComponentController::Item::Kind::Cleanup: return OpKind::CleanupImage;
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
    for (const auto& entry : catalog.components()) {
        if (entry.kind == ComponentCatalogEntry::Kind::Remove) {
            found.items[entry.id] = core::probeComponent(mountDir, entry.recipe);
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

    // System components this image has, in catalog order; the cleanup is always on offer.
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
        item.packageName = utf8::toWide(entry.id);
        if (entry.kind == ComponentCatalogEntry::Kind::Cleanup) {
            item.kind = Item::Kind::Cleanup;
            item.packageName = kCleanupTarget;
            item.identity = kCleanupCommand;
            item.contents = {std::wstring(kCleanupCommand) + (entry.resetBase ? L" /ResetBase" : L"")};
        } else {
            if (!probed) {
                continue;
            }
            const auto found = system->items.find(entry.id);
            if (found == system->items.end() || !found->second.present) {
                continue;
            }
            item.kind = Item::Kind::System;
            item.size = found->second.size;
            item.identity = entry.recipe.paths.front();
            item.contents = entry.recipe.packages;
            item.contents.insert(item.contents.end(), entry.recipe.paths.begin(), entry.recipe.paths.end());
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
    return kind == OpKind::RemoveAppx || kind == OpKind::RemoveComponent || kind == OpKind::CleanupImage;
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
    } else {
        op.value = item.name; // not used by the engine: the name the Apply lists show instead of the package id
    }
    return op;
}

bool ComponentController::queued(const Item& item) const {
    return m_state.changes().find(kindOf(item), item.packageName) != nullptr;
}

ComponentController::Check ComponentController::check(const Group& group) const {
    std::size_t n = 0;
    for (const auto& item : group.items) {
        n += queued(item) ? 1 : 0;
    }
    return n == 0 ? Check::Off : n == group.items.size() ? Check::On : Check::Partial;
}

void ComponentController::toggle(const Item& item) {
    if (!m_state.unqueue(kindOf(item), item.packageName)) {
        m_state.queue(operationFor(item));
    }
}

void ComponentController::toggleGroup(const Group& group) {
    const bool all = check(group) == Check::On;
    for (const auto& item : group.items) {
        if (all) {
            m_state.unqueue(kindOf(item), item.packageName);
        } else if (!queued(item)) {
            m_state.queue(operationFor(item));
        }
    }
}

void ComponentController::resetChanges() {
    m_state.unqueueIf([](const Operation& op) { return isComponentKind(op.kind); });
}

std::size_t ComponentController::queuedCount() const {
    return m_state.changes().count(OpKind::RemoveAppx) + m_state.changes().count(OpKind::RemoveComponent) +
           m_state.changes().count(OpKind::CleanupImage);
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
