#include "app/pages/overview/QueueChips.h"

#include <algorithm>
#include <cwchar>

namespace wl::app {

using core::ops::OpKind;

namespace {

bool claimed(const std::vector<QueueClaims::Slot>& slots, const core::ops::Operation& op) {
    return std::ranges::any_of(slots, [&](const QueueClaims::Slot& s) {
        return s.first == op.kind && _wcsicmp(s.second.c_str(), op.target.c_str()) == 0;
    });
}

} // namespace

PageId queuePageOf(const core::ops::Operation& op, const QueueClaims& claims) {
    if (claimed(claims.unattended, op)) {
        return PageId::Unattended;
    }
    if (claimed(claims.components, op)) {
        return PageId::Components;
    }
    if (claimed(claims.registry, op)) {
        return PageId::Registry;
    }
    switch (op.kind) {
    case OpKind::RemovePackage:
    case OpKind::RemoveAppx:
    case OpKind::RemoveComponent:
    case OpKind::CleanupImage:
    case OpKind::ShrinkStore: return PageId::Components;
    case OpKind::RemoveCapability:
    case OpKind::DisableFeature:
    case OpKind::EnableFeature: return PageId::Features;
    case OpKind::AddDriver:
    case OpKind::RemoveDriver: return PageId::Drivers;
    case OpKind::AddPackage: return op.value == L"language" ? PageId::Languages : PageId::Updates;
    case OpKind::SetIntl: return PageId::Languages;
    case OpKind::AddAppx:
    case OpKind::SetDefaultApps: return PageId::Apps;
    case OpKind::SetServiceStart: return PageId::Services;
    case OpKind::SetPolicy: return PageId::Policies;
    case OpKind::SetContextMenu: return PageId::ContextMenu;
    case OpKind::SetPostSetup: return PageId::PostSetup;
    case OpKind::CopyTree: return PageId::Files;
    case OpKind::SetEdition: return PageId::Images;
    case OpKind::SetTaskState:
    case OpKind::CreateTask: return PageId::Tasks;
    case OpKind::SetHosts:
    case OpKind::SetDns: return PageId::Hosts;
    case OpKind::SetPicture:
    case OpKind::AddFont: return PageId::Branding;
    case OpKind::PatchIcons: return PageId::Icons;
    case OpKind::SetRegistryValue:
    case OpKind::SetRegistryFirstLogon:
    case OpKind::WriteFile:
    case OpKind::CopyFile: return PageId::Tweaks;
    }
    return PageId::Apply; // no page: "Diğer"
}

std::vector<OverviewInputs::QueueGroup> queueGroups(const core::ops::ChangeSet& changes, const QueueClaims& claims,
                                                    const std::vector<QueueLayout>& layout) {
    std::vector<OverviewInputs::QueueGroup> groups;
    for (const auto& group : layout) {
        OverviewInputs::QueueGroup g{group.caption, {}};
        for (const auto& tab : group.tabs) {
            g.items.push_back({tab.tab, tab.label, 0});
        }
        groups.push_back(std::move(g));
    }
    int other = 0;
    for (const auto& op : changes.operations()) {
        const PageId page = queuePageOf(op, claims);
        bool counted = false;
        for (std::size_t g = 0; g < layout.size() && !counted; ++g) {
            for (std::size_t t = 0; t < layout[g].tabs.size() && !counted; ++t) {
                if (std::ranges::find(layout[g].tabs[t].pages, page) != layout[g].tabs[t].pages.end()) {
                    ++groups[g].items[t].count;
                    counted = true;
                }
            }
        }
        other += counted ? 0 : 1;
    }
    if (other > 0) {
        groups.push_back({Str::OverviewQueueOther, {{PageId::Apply, Str::OverviewQueueOtherItem, other}}});
    }
    return groups;
}

} // namespace wl::app
