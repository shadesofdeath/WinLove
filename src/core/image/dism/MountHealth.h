#pragma once
// Mount health (docs/ENGINE.md "Mount durumları"): what state a mount folder is in and what to
// do about it. DISM only reports Ok / NeedsRemount / Invalid; the failures seen in practice also
// need our own checks — the WIM deleted under a mount, leftovers from an interrupted mount with
// no DISM record (next mount fails 0xC1420116), registry hives still loaded from the mount
// (unmount fails 0xC1420117). Engine thread only; needs admin like Dism.
#include "core/image/dism/Dism.h"
#include "core/system/FileLocks.h"

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
    std::wstring imageName;                // edition name of record->index ("Windows 11 Pro"), if readable
    MountState state = MountState::Free;
    MountAction action = MountAction::None;
    bool windowsImage = false;             // Windows\System32\config\SOFTWARE present
    std::vector<std::wstring> loadedHives; // e.g. \REGISTRY\MACHINE\WL_SOFTWARE, loaded from inside
    std::vector<FolderBlocker> blockers;   // Explorer windows inside, processes holding files
};

// One folder (e.g. the WinLove mount folder).
[[nodiscard]] Result<MountCheck> inspectMount(Dism& dism, const std::filesystem::path& folder);
// Every mount DISM knows about (any folder).
[[nodiscard]] Result<std::vector<MountCheck>> inspectMounts(Dism& dism);

// Registry hives loaded (RegLoadKey) from files under `folder`, as \REGISTRY\... names.
[[nodiscard]] std::vector<std::wstring> hivesLoadedFrom(const std::filesystem::path& folder);
// Unloads them (needs SeBackup/SeRestore, enabled here). Done before every unmount.
[[nodiscard]] Result<void> unloadHivesUnder(const std::filesystem::path& folder);

// Carries out check.action. Returns the state afterwards (re-inspected). Moves Explorer windows
// out of the folder first; leftovers are removed with forceRemoveContents.
[[nodiscard]] Result<MountCheck> repairMount(Dism& dism, const MountCheck& check, const TaskContext& task);

// Unmount that survives the usual traps: unloads hives and moves Explorer windows away first; on
// "file in use" / partial unmount (0xC1420112 / 0xC1420117) releases again and retries; if the
// image is detached but leftovers remain, cleans the folder. The WIM commit happens before the
// detach step, so a partial unmount after commit keeps the changes.
// Mount that starts from a clean slate: moves Explorer away, repairs leftovers (orphaned /
// invalid / image missing), deletes and recreates the mount folder itself (a reused folder can
// keep stale wimmount state after a partial unmount: DISM then answers 0xC1420113 on an empty
// folder), mounts, and on "folder busy/not empty" codes cleans up and retries once.
// If the folder already holds a healthy mount of the same WIM + index, that mount is reused.
struct MountOutcome {
    bool reused = false;    // the image was already mounted there
    bool recovered = false; // needed a repair or a retry
};
[[nodiscard]] Result<MountOutcome> mountSafely(Dism& dism, const std::filesystem::path& wim, int index,
                                               const std::filesystem::path& folder, bool readOnly,
                                               const TaskContext& task);

struct UnmountOutcome {
    bool recovered = false; // needed retries or a folder repair
    int attempts = 0;
};
[[nodiscard]] Result<UnmountOutcome> unmountSafely(Dism& dism, const std::filesystem::path& folder, bool commit,
                                                   const TaskContext& task);

} // namespace wl::core
