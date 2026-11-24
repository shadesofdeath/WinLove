#pragma once
// D-141: a VHD / VHDX that holds an installed (or applied, never booted) Windows, as a source.
//
// "Mounting" one: WinLove creates a differencing child disk of its own (a .vhd / .vhdx in its work
// folder, parent = the user's file) and attaches the CHILD with virtdisk.dll — no drive letter. The
// Windows volume of the attached disk is found by its volume GUID path (the largest NTFS volume with
// Windows\System32\config\SYSTEM) and put on an empty folder as a volume mount point: that folder is
// the image root DISM opens (DismOpenSession), exactly like a mounted WIM's folder. Every write goes
// into the child. Saving = detach, merge the child into the user's file, delete the child. Discarding
// = detach, delete the child: the user's file was never written. Nothing of a WIM applies (one
// edition, no export / ISO / compression), see SourceCapabilities.
//
// Crash safety: every attach WinLove makes is written to a record file (vhd-mounts.json) before the
// disk is attached and removed after it is detached. A later start finds its own disk again ONLY by
// the recorded paths (child file, folder, volume GUID) — it never guesses, never enumerates and
// detaches anything else, never touches another disk or another tool's mount.
//
// Needs an elevated process (attaching a disk needs SeManageVolume). No UI.
#include "core/image/ImageFormat.h"
#include "core/image/ImageInfo.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct SourceInfo;

[[nodiscard]] bool isVirtualDiskFormat(ImageFormat format) noexcept;
[[nodiscard]] bool isVirtualDiskPath(const std::filesystem::path& path) noexcept;

// What a source can do (pure, unit tested): a VHD has one edition, no WIM to export / compress /
// verify, no setup media for an ISO / USB stick, and its changes go into a child disk (discardable).
struct SourceCapabilities {
    bool mountable = true;        // ESD / solid images: convert first
    bool wimEditions = true;      // the edition list is a WIM's: delete / rename / export / reorder / append
    bool verifiable = true;       // the WIM's streams can be checked (SHA-1)
    bool setupMedia = false;      // an ISO / USB stick can be made from it (setup files present)
    bool discardable = true;      // "discard" undoes everything since the mount
    bool virtualDisk = false;     // a VHD / VHDX (one installed Windows, mounted through a child disk)
};
[[nodiscard]] SourceCapabilities sourceCapabilities(const SourceInfo& source) noexcept;

// What WinLove reads about the disk itself (shown next to the edition).
struct VirtualDiskInfo {
    std::uint64_t virtualSize = 0;  // the disk's size as Windows sees it
    std::uint64_t fileSize = 0;     // the file on the host (a dynamic disk grows to what is written)
    bool dynamic = false;
    bool differencing = false;      // the user's file is itself a child of another disk
    std::filesystem::path parent;   // its parent then
    std::uint64_t volumeSize = 0;   // the Windows volume
    std::uint64_t volumeFree = 0;
    std::wstring volumeLabel;
    int volumes = 0;                // volumes on the disk (EFI, MSR, recovery … are not counted by Windows)
};

// ---- the Windows volume of an attached disk (pure) -------------------------------------------
struct VolumeCandidate {
    std::wstring guidPath;   // \\?\Volume{…}\  (trailing backslash)
    std::wstring fileSystem; // "NTFS"
    std::uint64_t size = 0;
    std::uint64_t freeBytes = 0;
    bool hasWindows = false; // Windows\System32\config\SYSTEM is there
};
// The largest NTFS volume that holds Windows\System32\config\SYSTEM; nullopt when none does.
[[nodiscard]] std::optional<std::size_t> chooseWindowsVolume(std::span<const VolumeCandidate> volumes) noexcept;
// "\\.\PhysicalDrive7" → 7; nullopt for anything else.
[[nodiscard]] std::optional<std::uint32_t> diskNumberFromPhysicalPath(std::wstring_view path) noexcept;
// The child disk WinLove creates for `vhd` in `workFolder`: "<stem>.winlove-changes.<ext>" (same
// format as the parent: a VHD's child is a VHD, a VHDX's a VHDX).
[[nodiscard]] std::filesystem::path changesDiskPath(const std::filesystem::path& vhd, const std::filesystem::path& workFolder);

// ---- the Windows inside, from its registry (offreg: nothing is loaded into this PC's registry) --
struct OfflineWindowsFacts {
    std::wstring productName;       // CurrentVersion\ProductName ("Windows 10 Pro" also on Windows 11)
    std::wstring editionId;         // "Professional"
    std::wstring installationType;  // "Client"
    std::wstring displayVersion;    // "25H2"
    int major = 10;
    int minor = 0;
    int build = 0;                  // CurrentBuildNumber
    int ubr = 0;                    // UBR
    std::wstring processorArchitecture; // Session Manager\Environment: AMD64 / ARM64 / x86
    std::vector<std::wstring> languages; // Control\MUI\UILanguages
    std::wstring defaultLanguage;   // from Control\Nls\Language\InstallLanguage
    std::wstring systemRoot;        // "WINDOWS"
};
// The one edition of the disk as an ImageInfo (index 1). Windows 11 still says "Windows 10" in
// ProductName: from build 22000 on the name says 11.
[[nodiscard]] ImageInfo imageInfoFromFacts(const OfflineWindowsFacts& facts, std::uint64_t usedBytes);
[[nodiscard]] Result<OfflineWindowsFacts> readWindowsFacts(const std::filesystem::path& windowsRoot);

// ---- the record of WinLove's own attaches -----------------------------------------------------
struct VhdMountRecord {
    std::filesystem::path vhd;    // the user's disk (the parent of `child`)
    std::filesystem::path child;  // WinLove's differencing disk: what is attached
    std::filesystem::path folder; // the image root: a volume mount point of the Windows volume
    std::wstring volume;          // \\?\Volume{…}\ of that volume (empty until it is known)
    std::int64_t attachedAt = 0;  // UTC seconds
    std::wstring by;              // "WinLove" / "wlcli"
};
// %LOCALAPPDATA%\WinLove\vhd-mounts.json unless set (wlcli --vhd-records=, tests).
[[nodiscard]] std::filesystem::path vhdRecordFile();
void setVhdRecordFile(std::filesystem::path file);
[[nodiscard]] std::vector<VhdMountRecord> parseVhdRecords(std::string_view json); // pure; bad input → empty / skipped
[[nodiscard]] std::string serializeVhdRecords(const std::vector<VhdMountRecord>& records);
[[nodiscard]] std::vector<VhdMountRecord> loadVhdRecords();
[[nodiscard]] Result<void> saveVhdRecords(const std::vector<VhdMountRecord>& records);
[[nodiscard]] std::optional<VhdMountRecord> vhdRecordForFolder(const std::filesystem::path& folder);
[[nodiscard]] std::optional<VhdMountRecord> vhdRecordForDisk(const std::filesystem::path& vhd);

// State of one record (pure). Only ever about the recorded child, folder and volume.
enum class VhdMountState : std::uint8_t {
    Ok,           // the child is attached and the folder is on its Windows volume
    NotAttached,  // the child exists but is not attached (a reboot detaches every virtual disk)
    NoMountPoint, // attached, but the folder is not (or no longer) on its volume
    ChildMissing, // the child file is gone: nothing is attached, nothing to save
    DiskMissing,  // the user's disk is gone (an unplugged drive?): nothing is done until it is back
};
enum class VhdAction : std::uint8_t {
    None,
    Remount, // attach the child again / put the folder on its volume again (the changes are kept)
    Forget,  // nothing attached, nothing to save: drop the record (and the folder if it is empty)
};
[[nodiscard]] const wchar_t* vhdMountStateName(VhdMountState state) noexcept;
[[nodiscard]] const wchar_t* vhdActionName(VhdAction action) noexcept;
[[nodiscard]] VhdMountState classifyVhdMount(bool diskExists, bool childExists, bool attached, bool folderOnVolume) noexcept;
[[nodiscard]] VhdAction recommendedVhdAction(VhdMountState state) noexcept;

struct VhdMountCheck {
    VhdMountRecord record;
    VhdMountState state = VhdMountState::Ok;
    VhdAction action = VhdAction::None;
    std::uint64_t childSize = 0; // bytes of changes held in the child
};
[[nodiscard]] std::vector<VhdMountCheck> inspectVhdMounts();
[[nodiscard]] std::optional<VhdMountCheck> inspectVhdMount(const std::filesystem::path& folder);

// ---- operations (elevated) -------------------------------------------------------------------
// The disk as a source: when WinLove has it mounted, read through its folder; else attached read
// only for the moment of reading (no record, no drive letter, detached again; a crash detaches it
// too — only a mount is attached for longer than the process).
[[nodiscard]] Result<SourceInfo> openVirtualDiskSource(const std::filesystem::path& vhd);
// Child disk in `workFolder`, attached, Windows volume on `folder` (empty or missing). Recorded.
[[nodiscard]] Result<VhdMountRecord> mountVirtualDisk(const std::filesystem::path& vhd, const std::filesystem::path& folder,
                                                      const std::filesystem::path& workFolder, std::wstring_view by,
                                                      const TaskContext& task);
struct VhdUnmountOutcome {
    bool merged = false;      // the changes are in the user's disk
    bool discarded = false;   // the child was deleted unmerged
    bool locked = true;       // the volume could be locked (no open handle) before the detach
    std::uint64_t changeBytes = 0; // the child's size before it went
};
// Hives under the volume unloaded, the mount point removed, the volume locked + dismounted, the
// child detached; then `commit` merges it into the user's disk, else it is deleted. The record goes.
[[nodiscard]] Result<VhdUnmountOutcome> unmountVirtualDisk(const std::filesystem::path& folder, bool commit, const TaskContext& task);
// Carries out check.action (Remount / Forget). Returns the state afterwards.
[[nodiscard]] Result<VhdMountCheck> repairVirtualDisk(const VhdMountCheck& check, const TaskContext& task);
// Whether WinLove's own changes disk `file` is attached; nullopt when no open of it answered (then
// nothing is merged or deleted). Opens it read / write: never asked of the user's disk.
[[nodiscard]] std::optional<bool> changesDiskAttached(const std::filesystem::path& file);

} // namespace wl::core
