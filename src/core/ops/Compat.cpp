#include "core/ops/Compat.h"

#include "base/Utf8.h"
#include "core/image/SystemComponents.h"

#include <windows.h>

#include <algorithm>
#include <optional>

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

// The longest catalog prefixes are names in their own right: "Microsoft.VCLibs" covers
// "Microsoft.VCLibs.140.00" and its ".UWPDesktop" sibling, not "Microsoft.VCLibsX".
bool keepsApp(const CompatGuard& guard, std::wstring_view identity) {
    return std::ranges::any_of(guard.appx, [&](const std::wstring& prefix) {
        return startsWith(identity, prefix) &&
               (identity.size() == prefix.size() || identity[prefix.size()] == L'.' || identity[prefix.size()] == L'_');
    });
}

// D-121: what a component's recipe takes along that a guard keeps — one of its services (the keys
// go with the files) or one of its apps. The reference tool locks the component a locked service belongs to;
// before, "Printing" kept the spooler from being disabled but not the Printing component (and the
// spooler with it) from being removed.
bool takesWhatItKeeps(const CompatGuard& guard, const ComponentRecipe& recipe) {
    return std::ranges::any_of(recipe.services, [&](const std::wstring& service) { return contains(guard.services, service); }) ||
           std::ranges::any_of(recipe.appx, [&](const std::wstring& app) { return keepsApp(guard, appxIdentity(app)); });
}

CompatBlock blockWith(const Operation& op, std::span<const CompatGuard> active, const AppxNeeds& needs,
                      const std::vector<std::wstring>& removed) {
    CompatBlock block;
    switch (op.kind) {
    case OpKind::RemoveAppx: {
        const std::wstring identity = appxIdentity(op.target);
        for (const auto& guard : active) {
            if (keepsApp(guard, identity)) {
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
    case OpKind::ShrinkStore: {
        // A component's 32-bit row ("tabletpc:32", D-113) is held with the component.
        std::wstring id = op.target;
        if (id.size() > 3 && id.ends_with(L":32")) {
            id.resize(id.size() - 3);
        }
        std::optional<ComponentRecipe> recipe; // read once, when a guard needs it
        for (const auto& guard : active) {
            if (contains(guard.components, op.target) || contains(guard.components, id)) {
                block.guards.push_back(guard.id);
                continue;
            }
            if (op.kind != OpKind::RemoveComponent || guard.always || (guard.services.empty() && guard.appx.empty())) {
                continue;
            }
            if (!recipe) {
                auto parsed = componentRecipeFromJson(utf8::fromWide(op.value));
                recipe = parsed ? std::move(*parsed) : ComponentRecipe{};
            }
            if (takesWhatItKeeps(guard, *recipe)) {
                block.guards.push_back(guard.id);
            }
        }
        break;
    }
    case OpKind::RemoveCapability: {
        // "Edge.Webview2.Platform~~~~" / "VBSCRIPT~~~~0.0.1.0": the name before the first '~'.
        const std::wstring name = op.target.substr(0, op.target.find(L'~'));
        for (const auto& guard : active) {
            if (contains(guard.capabilities, name)) {
                block.guards.push_back(guard.id);
            }
        }
        break;
    }
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
