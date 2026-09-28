#include "app/controllers/ComponentController.h"

#include "base/Log.h"

#include <algorithm>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;

ComponentController::ComponentController(AppState& state, AppxCatalog catalog, Language language,
                                         std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_catalog(std::move(catalog)), m_language(language), m_post(std::move(postToUi)) {}

ComponentController::~ComponentController() {
    *m_alive = false;
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

std::vector<ComponentController::Group> ComponentController::groups() const {
    std::vector<Group> groups(m_catalog.groups().size());
    for (std::size_t g = 0; g < groups.size(); ++g) {
        groups[g].catalogIndex = static_cast<int>(g);
        groups[g].name = m_catalog.groups()[g].name(m_language);
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
        const int g = item.entry ? m_catalog.groupIndex(item.entry->group) : other;
        auto& group = groups[static_cast<std::size_t>(g < 0 ? other : g)];
        group.size += item.size;
        group.items.push_back(std::move(item));
    }
    std::erase_if(groups, [](const Group& g) { return g.items.empty(); });
    for (auto& g : groups) {
        std::ranges::sort(g.items, [](const Item& a, const Item& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    }
    return groups;
}

Operation ComponentController::operationFor(const Item& item) {
    Operation op{OpKind::RemoveAppx, item.packageName};
    op.risk = item.risk;
    op.sizeDelta = -static_cast<std::int64_t>(item.size);
    return op;
}

bool ComponentController::queued(const Item& item) const {
    return m_state.changes().find(OpKind::RemoveAppx, item.packageName) != nullptr;
}

ComponentController::Check ComponentController::check(const Group& group) const {
    std::size_t n = 0;
    for (const auto& item : group.items) {
        n += queued(item) ? 1 : 0;
    }
    return n == 0 ? Check::Off : n == group.items.size() ? Check::On : Check::Partial;
}

void ComponentController::toggle(const Item& item) {
    if (!m_state.unqueue(OpKind::RemoveAppx, item.packageName)) {
        m_state.queue(operationFor(item));
    }
}

void ComponentController::toggleGroup(const Group& group) {
    const bool all = check(group) == Check::On;
    for (const auto& item : group.items) {
        if (all) {
            m_state.unqueue(OpKind::RemoveAppx, item.packageName);
        } else if (!queued(item)) {
            m_state.queue(operationFor(item));
        }
    }
}

void ComponentController::resetChanges() {
    m_state.unqueueIf([](const Operation& op) { return op.kind == OpKind::RemoveAppx; });
}

std::size_t ComponentController::queuedCount() const {
    return m_state.changes().count(OpKind::RemoveAppx);
}

std::uint64_t ComponentController::queuedBytes() const {
    std::uint64_t total = 0;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind == OpKind::RemoveAppx && op.sizeDelta < 0) {
            total += static_cast<std::uint64_t>(-op.sizeDelta);
        }
    }
    return total;
}

} // namespace wl::app
