#pragma once
// D-145: WinLove replaces its own exe with a newer release's — only when the user clicked "Yükle".
// No UI here (the app and `wlcli update-install` drive the same steps):
//   1. pickUpdateAsset: the release's file that is WinLove.exe (every release so far has exactly one:
//      "WinLove.exe", application/x-msdownload, with GitHub's "digest": "sha256:…")
//   2. downloadUpdate: into "<exe>.new" next to the exe (same volume: the swap is two renames),
//      resumable, from GitHub's release download only (the redirect must stay on GitHub's storage)
//   3. verifyUpdateFile: the size GitHub listed, the SHA-256 GitHub listed (when it lists one), a PE
//      image for x64 with the GUI subsystem, a version resource whose product version is the release's
//   4. swapInUpdate: "<exe>" → "<exe>.old" (allowed while it runs), "<exe>.new" → "<exe>"; when the
//      second rename fails the first is undone. rollbackUpdate undoes a swap whose new exe did not start.
//   5. the new instance deletes "<exe>.old" once the old process is gone (removeUpdateBackup).
// Nothing runs elevated for this, nothing else is downloaded, nothing is sent but the download's GET.
#include "base/Result.h"
#include "core/net/ReleaseCheck.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace wl::core {

// The asset that becomes WinLove.exe: named "WinLove.exe" (any case), else a "WinLove…x64….exe" /
// "…amd64….exe"; uploaded, not empty, at most 512 MB, from the project's release downloads. nullopt:
// the release has none (a zip, another architecture, a broken upload) — the app then offers the page.
[[nodiscard]] std::optional<ReleaseAsset> pickUpdateAsset(const Release& release);
// https://github.com/shadesofdeath/WinLove/releases/download/… or the storage GitHub redirects a
// release download to (https, a host under githubusercontent.com).
[[nodiscard]] bool trustedAssetUrl(std::wstring_view url);

struct UpdatePaths {
    std::filesystem::path exe;    // the running WinLove.exe
    std::filesystem::path staged; // "<exe>.new": the download, verified, waiting for the swap
    std::filesystem::path backup; // "<exe>.old": the exe the swap moved aside
};
[[nodiscard]] UpdatePaths updatePaths(const std::filesystem::path& exe);

// A file can be made (and removed) in `folder`: else the update cannot be put there (Program Files
// without rights, a read-only share) and the app offers the release page instead.
[[nodiscard]] Result<void> checkFolderWritable(const std::filesystem::path& folder);

// Downloads the asset to updatePaths(exe).staged. `userAgent`: "WinLove/<version>" (nothing else
// about the machine or the user). Progress: the task's fraction; `bytes` (optional): done / total.
[[nodiscard]] Result<std::filesystem::path> downloadUpdate(const ReleaseAsset& asset, const std::filesystem::path& exe,
                                                           std::wstring_view userAgent, const TaskContext& task,
                                                           const std::function<void(std::uint64_t, std::uint64_t)>& bytes = {});

// What a file says about itself (read without running it).
struct ExeFacts {
    std::uint64_t size = 0;
    std::wstring sha256;          // lower-case hex
    bool pe = false;              // "MZ" … "PE\0\0", an executable image, not a DLL
    std::uint16_t machine = 0;    // 0x8664 = x64
    bool pe32Plus = false;        // optional header magic 0x20B
    std::uint16_t subsystem = 0;  // 2 = Windows GUI
    std::wstring productVersion;  // the version resource's ProductVersion ("1.3.0 Beta")
    std::wstring fileVersion;     // its fixed file version ("1.3.0.0")
    std::optional<AppVersion> version; // ProductVersion read as a WinLove version
};
// Reads the PE headers, the version resource and (with `hash`) the SHA-256. IoError when the file
// cannot be read at all; a file that is not a PE is a success with pe = false.
[[nodiscard]] Result<ExeFacts> readExeFacts(const std::filesystem::path& file, bool hash, const TaskContext& task = {});

struct UpdateExpectation {
    std::uint64_t size = 0;            // 0: not checked
    std::wstring sha256;               // empty: not checked (the release lists no digest)
    std::optional<AppVersion> version; // the release's
};
enum class VerifyFailure : std::uint8_t { Size, Hash, NotPe, NotX64, NotGui, NoVersion, VersionMismatch };
struct UpdateVerdict {
    ExeFacts facts;
    std::optional<VerifyFailure> failure; // nullopt: the file is the release's WinLove.exe
    std::wstring detail;                  // what differed, in English (logs, wlcli)
};
[[nodiscard]] Result<UpdateVerdict> verifyUpdateFile(const std::filesystem::path& file, const UpdateExpectation& expect,
                                                     const TaskContext& task = {});
[[nodiscard]] const wchar_t* verifyFailureName(VerifyFailure failure) noexcept;

// The swap. A stale "<exe>.old" (an earlier update whose process is gone) is removed first; when it
// cannot be (still in use), nothing is changed. `afterBackup` (tests, `wlcli --fail-at=swap`): runs
// between the two renames; its error is handled like a failed second rename (the backup goes back).
struct SwapHooks {
    std::function<Result<void>()> afterBackup;
};
[[nodiscard]] Result<void> swapInUpdate(const std::filesystem::path& exe, const std::filesystem::path& staged,
                                        const SwapHooks& hooks = {});
// Undoes a swap: the new exe goes back to "<exe>.new", the backup back to "<exe>". Nothing to undo
// (no backup): success, nothing changed.
[[nodiscard]] Result<void> rollbackUpdate(const std::filesystem::path& exe);
// "<exe>.old" deleted (the new instance, once the old one has exited); true when it is gone (or was
// never there). A file still in use stays: the next start tries again.
bool removeUpdateBackup(const std::filesystem::path& exe);

} // namespace wl::core
