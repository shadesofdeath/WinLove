#pragma once
// Mount health (docs/ENGINE.md "Mount durumları"): what state a mount folder is in and what to
// do about it. DISM only reports Ok / NeedsRemount / Invalid; the failures seen in practice also
// need our own checks — the WIM deleted under a mount, leftovers from an interrupted mount with
// no DISM record (next mount fails 0xC1420116), registry hives still loaded from the mount
// (unmount fails 0xC1420117). Engine thread only; needs admin like Dism.
#include "core/image/dism/Dism.h"

#include <optional>

namespace wl::core {

enum class MountState : std::uint8_t {
    Free,         // no DISM record, folder missing or empty — ready to mount
    Ok,           // DISM ok and the WIM still exists — usable
    NeedsRemount, // DISM "needs remount" (after a reboot the WIM filter is detached)
    Invalid,      // DISM "invalid": nothing can be committed
    ImageMissing, // DISM record but the WIM file is gone: commit impossible
    Orphaned,     // files in the folder but no DISM record (interrupted mount/unmount)
};
[[nodiscard]] const wchar_t* mountStateName(MountState state) noexcept;

enum class MountAction : std::uint8_t {
    None,        // Free, Ok
    Remount,     // DismRemountImage
    Discard,     // unload hives → unmount without commit → DismCleanupMountpoints
    ClearFolder, // DismCleanupMountpoints → delete leftovers in the folder
};
[[nodiscard]] const wchar_t* mountActionName(MountAction action) noexcept;
[[nodiscard]] MountAction recommendedAction(MountState state) noexcept;

// Pure classification (unit-tested): DISM record for the folder, does its WIM exist, does the
// folder contain anything.
[[nodiscard]] MountState classifyMount(const std::optional<MountInfo>& record, bool imageExists,
                                       bool folderHasEntries) noexcept;

struct MountCheck {
    std::filesystem::path folder;
    std::optional<MountInfo> record;       // DISM's view; empty for Free / Orphaned
    MountState state = MountState::Free;
    MountAction action = MountAction::None;
    bool windowsImage = false;             // Windows\System32\config\SOFTWARE present
    std::vector<std::wstring> loadedHives; // e.g. \REGISTRY\MACHINE\WL_SOFTWARE, loaded from inside
};

// One folder (e.g. the WinLove mount folder).
[[nodiscard]] Result<MountCheck> inspectMount(Dism& dism, const std::filesystem::path& folder);
// Every mount DISM knows about (any folder).
[[nodiscard]] Result<std::vector<MountCheck>> inspectMounts(Dism& dism);

// Registry hives loaded (RegLoadKey) from files under `folder`, as \REGISTRY\... names.
[[nodiscard]] std::vector<std::wstring> hivesLoadedFrom(const std::filesystem::path& folder);
// Unloads them (needs SeBackup/SeRestore, enabled here). Done before every unmount.
[[nodiscard]] Result<void> unloadHivesUnder(const std::filesystem::path& folder);

// Carries out check.action. Returns the state afterwards (re-inspected).
[[nodiscard]] Result<MountCheck> repairMount(Dism& dism, const MountCheck& check, const TaskContext& task);

} // namespace wl::core
