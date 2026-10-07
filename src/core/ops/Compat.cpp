#include "core/ops/Compat.h"

#include <windows.h>

#include <algorithm>

namespace wl::core::ops {

namespace {

bool same(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool startsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && same(text.substr(0, prefix.size()), prefix);
}

bool contains(const std::vector<std::wstring>& list, std::wstring_view value) {
    return std::ranges::any_of(list, [&](const std::wstring& item) { return same(item, value); });
}

// The identity names of the apps `queue` removes.
std::vector<std::wstring> removedApps(const ChangeSet& queue) {
    std::vector<std::wstring> out;
    for (const auto& op : queue.operations()) {
        if (op.kind == OpKind::RemoveAppx) {
            out.push_back(appxIdentity(op.target));
        }
    }
    return out;
}

CompatBlock blockWith(const Operation& op, std::span<const CompatGuard> active, const AppxNeeds& needs,
                      const std::vector<std::wstring>& removed) {
    CompatBlock block;
    switch (op.kind) {
    case OpKind::RemoveAppx: {
        const std::wstring identity = appxIdentity(op.target);
        for (const auto& guard : active) {
            // The longest catalog prefixes are names in their own right: "Microsoft.VCLibs" covers
            // "Microsoft.VCLibs.140.00" and its ".UWPDesktop" sibling, not "Microsoft.VCLibsX".
            const bool guarded = std::ranges::any_of(guard.appx, [&](const std::wstring& prefix) {
                return startsWith(identity, prefix) &&
                       (identity.size() == prefix.size() || identity[prefix.size()] == L'.' || identity[prefix.size()] == L'_');
            });
            if (guarded) {
                block.guards.push_back(guard.id);
            }
        }
        for (const auto& [app, list] : needs) {
            if (!same(app, identity) && contains(list, identity) && !contains(removed, app)) {
                block.neededBy.push_back(app);
            }
        }
        break;
    }
    case OpKind::RemoveComponent:
    case OpKind::CleanupImage:
    case OpKind::ShrinkStore:
        for (const auto& guard : active) {
            if (contains(guard.components, op.target)) {
                block.guards.push_back(guard.id);
            }
        }
        break;
    case OpKind::SetServiceStart:
        if (op.value == L"disabled") {
            for (const auto& guard : active) {
                if (contains(guard.services, op.target)) {
                    block.guards.push_back(guard.id);
                }
            }
        }
        break;
    default: break;
    }
    return block;
}

} // namespace

std::wstring appxIdentity(std::wstring_view packageFullName) {
    return std::wstring(packageFullName.substr(0, packageFullName.find(L'_')));
}

CompatBlock compatBlock(const Operation& op, std::span<const CompatGuard> active, const AppxNeeds& needs, const ChangeSet& queue) {
    return blockWith(op, active, needs, removedApps(queue));
}

std::vector<CompatConflict> compatConflicts(const ChangeSet& queue, std::span<const CompatGuard> active, const AppxNeeds& needs) {
    std::vector<CompatConflict> out;
    // Apps leave together with the runtimes only they needed; one that stays keeps them. Repeated
    // until nothing changes: a runtime freed only by an app that is itself blocked stays blocked.
    std::vector<std::wstring> removed = removedApps(queue);
    std::vector<bool> blocked(queue.operations().size(), false);
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t i = 0; i < queue.operations().size(); ++i) {
            const auto& op = queue.operations()[i];
            if (blocked[i]) {
                continue;
            }
            if (auto block = blockWith(op, active, needs, removed); !block.empty()) {
                blocked[i] = true;
                changed = true;
                if (op.kind == OpKind::RemoveAppx) {
                    const std::wstring identity = appxIdentity(op.target);
                    std::erase_if(removed, [&](const std::wstring& r) { return same(r, identity); });
                }
            }
        }
    }
    for (std::size_t i = 0; i < queue.operations().size(); ++i) {
        if (blocked[i]) {
            const auto& op = queue.operations()[i];
            out.push_back({op, blockWith(op, active, needs, removed)});
        }
    }
    return out;
}

} // namespace wl::core::ops
