#include "app/controllers/ContextMenuController.h"

#include "base/Log.h"
#include "base/Text.h"

#include <algorithm>

namespace wl::app {

using core::ContextMenuEntry;
using core::ContextMenuItem;
using core::ops::OpKind;
using core::ops::Operation;

namespace {
constexpr std::wstring_view kEntryPrefix = L"entry\\";
constexpr std::wstring_view kAddPrefix = L"add\\";
} // namespace

ContextMenuController::ContextMenuController(AppState& state, std::function<void(std::function<void()>)> postToUi)
    : m_state(state), m_post(std::move(postToUi)) {}

ContextMenuController::~ContextMenuController() {
    *m_alive = false;
}

void ContextMenuController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.contextMenuList();
    if (!force && current && current->mountDir == mounted->mountDir &&
        current->status != AppState::ContextMenuList::Status::Failed) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setContextMenuList(AppState::ContextMenuList{AppState::ContextMenuList::Status::Loading, mountDir, {}, {}});
    auto post = m_post;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<std::vector<ContextMenuEntry>>(
        [mountDir](const core::TaskContext&) { return core::readContextMenu(mountDir); },
        [this, post, alive, mountDir](Result<std::vector<ContextMenuEntry>> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& now = m_state.contextMenuList();
                if (!m_state.mounted() || m_state.mounted()->mountDir != mountDir || !now ||
                    now->status != AppState::ContextMenuList::Status::Loading || now->mountDir != mountDir) {
                    return;
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    m_state.setContextMenuList(
                        AppState::ContextMenuList{AppState::ContextMenuList::Status::Failed, mountDir, {}, result.error()});
                    return;
                }
                m_state.setContextMenuList(
                    AppState::ContextMenuList{AppState::ContextMenuList::Status::Ready, mountDir, std::move(*result), {}});
            });
        },
        {});
}

std::wstring ContextMenuController::entryTarget(const ContextMenuEntry& entry) {
    return std::wstring(kEntryPrefix) + entry.id;
}

Operation ContextMenuController::operationFor(const ContextMenuEntry& entry, bool hidden) {
    core::ContextMenuOperation op;
    op.name = entry.name;
    op.writes = hidden ? core::contextMenuHideWrites(entry) : core::contextMenuShowWrites(entry);
    Operation out{OpKind::SetContextMenu, entryTarget(entry), core::contextMenuOperationJson(op)};
    out.risk = core::ops::Risk::Low;
    return out;
}

Result<Operation> ContextMenuController::operationFor(const ContextMenuItem& item) {
    auto writes = core::contextMenuAddWrites(item);
    if (!writes) {
        return std::unexpected(writes.error());
    }
    core::ContextMenuOperation op{item.name, item, std::move(*writes)};
    Operation out{OpKind::SetContextMenu, std::wstring(kAddPrefix) + item.id, core::contextMenuOperationJson(op)};
    out.risk = core::ops::Risk::Low;
    return out;
}

bool ContextMenuController::changed(const ContextMenuEntry& entry) const {
    return m_state.changes().find(OpKind::SetContextMenu, entryTarget(entry)) != nullptr;
}

bool ContextMenuController::hidden(const ContextMenuEntry& entry) const {
    return changed(entry) ? !entry.hidden : entry.hidden;
}

void ContextMenuController::setHidden(const ContextMenuEntry& entry, bool hide) {
    if (hide == entry.hidden) {
        m_state.unqueue(OpKind::SetContextMenu, entryTarget(entry));
        return;
    }
    m_state.queue(operationFor(entry, hide));
}

std::vector<ContextMenuItem> ContextMenuController::items() const {
    std::vector<ContextMenuItem> out;
    for (const auto& op : m_state.changes().operations()) {
        if (op.kind != OpKind::SetContextMenu || !op.target.starts_with(kAddPrefix)) {
            continue;
        }
        if (auto parsed = core::parseContextMenuOperation(op.value); parsed && parsed->item) {
            out.push_back(std::move(*parsed->item));
        }
    }
    return out;
}

Result<void> ContextMenuController::addItem(ContextMenuItem item) {
    if (item.id.empty()) {
        item.id = core::contextMenuItemId(item.name);
    }
    const auto queued = items();
    const std::wstring base = item.id;
    for (int n = 2; std::ranges::any_of(queued, [&](const ContextMenuItem& i) { return text::iequals(i.id, item.id); }); ++n) {
        item.id = base + L"-" + std::to_wstring(n);
    }
    auto op = operationFor(item);
    if (!op) {
        return std::unexpected(op.error());
    }
    m_state.queue(std::move(*op));
    return {};
}

void ContextMenuController::removeItem(const std::wstring& id) {
    m_state.unqueue(OpKind::SetContextMenu, std::wstring(kAddPrefix) + id);
}

void ContextMenuController::resetChanges() {
    m_state.unqueueIf([](const Operation& op) { return op.kind == OpKind::SetContextMenu; });
}

int ContextMenuController::changedCount() const {
    return static_cast<int>(m_state.changes().count(OpKind::SetContextMenu));
}

} // namespace wl::app
