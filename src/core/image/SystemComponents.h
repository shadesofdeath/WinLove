#pragma once
// P07 system components (docs/pages/07-components.md §2, D-031): things Windows ships outside the
// provisioned-app list and DISM has no "remove" for — Edge, the WebView2 runtime, OneDrive setup,
// the recovery image. A component is a recipe:
//   packages  CBS package families. Each installed identity is first unlocked in the image's
//             SOFTWARE hive (Component Based Servicing\Packages\<identity>: Visibility = 1, the
//             Owners key removed — a hidden, parent-owned package is "permanent" to DISM,
//             0x800F0825), then removed with DismRemovePackage.
//   paths     files / folders relative to the image root, removed whole (TrustedInstaller ACLs
//             are taken over entry by entry; never through a reparse point).
//   registry  offline writes (delete the Run value, the uninstall entry, the service key…).
//   driverClasses  device class GUIDs whose inbox drivers are taken out of the image and its
//             component store (deep removal, D-060: DeepRemoval.h; legacy classes only).
// In the queue a recipe is the value of one RemoveComponent operation (target = the catalog id),
// so a preset carries what it does and the Applier needs no catalog.
#include "core/image/RegistryEdit.h"
#include "core/image/dism/Dism.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct ComponentRecipe {
    std::wstring title; // as the Apply list and presets show it
    std::vector<std::wstring> packages;
    std::vector<std::wstring> paths;
    std::vector<RegistryWrite> registry;
    std::vector<std::wstring> driverClasses;

    [[nodiscard]] bool operator==(const ComponentRecipe&) const = default;
};

[[nodiscard]] std::string componentRecipeToJson(const ComponentRecipe& recipe);
[[nodiscard]] Result<ComponentRecipe> componentRecipeFromJson(std::string_view json);
// "title" of a RemoveComponent / CleanupImage operation value; empty when it cannot be read.
[[nodiscard]] std::wstring componentTitle(std::wstring_view operationValue);

// A recipe is user input (it arrives in preset files): paths must be relative, at least two
// levels deep, without "." / ".." / drive or stream syntax, and none of the folders Windows
// cannot live without; package families are plain names; registry keys must map into the image.
[[nodiscard]] Result<void> validateComponentRecipe(const ComponentRecipe& recipe);
// <mountDir>\<relative>, refused when a folder on the way is a reparse point (an image's
// "Documents and Settings" junction points at the HOST's C:\Users).
[[nodiscard]] Result<std::filesystem::path> resolveImagePath(const std::filesystem::path& mountDir,
                                                             std::wstring_view relative);

// ---- CBS packages ------------------------------------------------------------------------------
struct CbsPackage {
    std::wstring identity;       // "Microsoft-Windows-OneDrive-Setup-Package~31bf3856ad364e35~amd64~~10.0.26100.5074"
    std::uint32_t visibility = 0; // 1 visible, 2 hidden
    std::uint32_t state = 0;      // CurrentState: 0x40 staged … 0x70 installed
};
inline constexpr std::uint32_t kCbsStaged = 0x40;
inline constexpr std::uint32_t kCbsInstalled = 0x70;
inline constexpr const wchar_t* kCbsPackagesKey =
    L"Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\Packages";

// "Name~token~arch~lang~version" → "Name".
[[nodiscard]] std::wstring_view cbsPackageFamily(std::wstring_view identity);
[[nodiscard]] bool cbsLanguageNeutral(std::wstring_view identity);
// The identities of `families` worth removing (staged or more), in removal order: language
// packages before their neutral package, installed before staged leftovers of older versions.
[[nodiscard]] std::vector<CbsPackage> cbsRemovalOrder(const std::vector<std::wstring>& families,
                                                      const std::vector<CbsPackage>& all);
// Every package under an open "…\Component Based Servicing\Packages" key.
[[nodiscard]] std::vector<CbsPackage> readCbsPackages(HKEY packagesKey);
// Visibility = 1 and no Owners, for one identity under that key.
[[nodiscard]] Result<void> unlockCbsPackage(HKEY packagesKey, const std::wstring& identity);
// The whole list of a mounted image (loads its SOFTWARE hive; elevated process).
[[nodiscard]] Result<std::vector<CbsPackage>> readCbsPackages(const std::filesystem::path& mountDir);

// ---- presence / removal ------------------------------------------------------------------------
struct ComponentPresence {
    bool present = false;   // at least one of the recipe's paths exists
    std::uint64_t size = 0; // bytes under those paths (files are counted where they are listed)
};
class ComponentStoreIndex;
// With an index (D-059), a recipe's packages count too: present when one of its families is
// installed, and the size is the larger of the paths' bytes and what only its packages own.
[[nodiscard]] ComponentPresence probeComponent(const std::filesystem::path& mountDir, const ComponentRecipe& recipe,
                                               const ComponentStoreIndex* store = nullptr);

// Runs the recipe against the session's image. The session is closed while the hives are edited
// (DISM keeps them to itself) and reopened for the package removal.
// Packages are best effort when the recipe also has paths: a package DISM refuses is logged as a
// warning and the files are removed anyway (the component is gone, its WinSxS copy stays).
[[nodiscard]] Result<void> removeComponent(DismSession& session, const ComponentRecipe& recipe, const TaskContext& task);

} // namespace wl::core
