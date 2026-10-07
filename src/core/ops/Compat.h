#pragma once
// Compatibility guards (D-082): what keeps a removal or a disabled service out of the queue, so
// that an image does not lose what a kept app or a feature the user wants needs. Two kinds:
//  - an app's own dependencies (AppxManifest.xml <PackageDependency>, read with the app list): a
//    runtime a kept app needs is not removed. Always on; removing the apps too frees it.
//  - guards the user keeps on (resources/catalog/compat.json: "Windows Update", "Printing" …):
//    each names the apps, system components, services and steps it needs.
#include "core/ops/ChangeSet.h"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wl::core::ops {

struct CompatGuard {
    std::wstring id;
    std::vector<std::wstring> appx;       // identity-name prefixes ("Microsoft.WindowsStore")
    std::vector<std::wstring> components; // RemoveComponent / CleanupImage / ShrinkStore targets (catalog ids)
    std::vector<std::wstring> services;   // services that must not be disabled
};

// A provisioned app (identity name) → the identity names it needs. Apps not in the image are absent.
using AppxNeeds = std::map<std::wstring, std::vector<std::wstring>>;

struct CompatBlock {
    std::vector<std::wstring> guards;   // ids of the active guards that keep it
    std::vector<std::wstring> neededBy; // identity names of the kept apps that need it
    [[nodiscard]] bool empty() const noexcept { return guards.empty() && neededBy.empty(); }
};

// "Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe" → "Microsoft.VCLibs.140.00".
[[nodiscard]] std::wstring appxIdentity(std::wstring_view packageFullName);

// What keeps `op` out of `queue` (the queue it would join, or is in: apps queued for removal do not
// hold their runtimes). Empty when nothing does.
[[nodiscard]] CompatBlock compatBlock(const Operation& op, std::span<const CompatGuard> active, const AppxNeeds& needs,
                                      const ChangeSet& queue);

// The operations of `queue` something keeps out, in queue order (index + why). After a guard is
// switched on or an app is taken back off the queue, these leave it.
struct CompatConflict {
    Operation op;
    CompatBlock block;
};
[[nodiscard]] std::vector<CompatConflict> compatConflicts(const ChangeSet& queue, std::span<const CompatGuard> active,
                                                          const AppxNeeds& needs);

} // namespace wl::core::ops
