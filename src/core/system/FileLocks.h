#pragma once
// Who keeps a folder busy, and how to get it free (mount folder hygiene, docs/ENGINE.md).
// The classic case: an Explorer window showing a folder inside the mount directory. DISM then
// unmounts "partially" (0xC1420117): the WIM is detached but that folder stays behind, and the
// next mount fails (0x80070005 / 0xC1420114).
#include "base/Result.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct FolderBlocker {
    enum class Kind : std::uint8_t { ExplorerWindow, Process };
    Kind kind = Kind::Process;
    std::wstring name;           // "explorer.exe", "notepad.exe"
    DWORD pid = 0;
    std::filesystem::path path;  // the folder the window shows / a file the process holds
};

// Explorer windows and tabs whose current folder is `folder` or inside it (IShellWindows).
[[nodiscard]] std::vector<FolderBlocker> explorerWindowsIn(const std::filesystem::path& folder);
// Navigates those windows to the folder's parent (does not close them). Returns how many moved.
int releaseExplorerWindows(const std::filesystem::path& folder);

// Processes holding any of `files` open (Restart Manager). Files that do not exist are skipped.
[[nodiscard]] std::vector<FolderBlocker> processesUsing(const std::vector<std::filesystem::path>& files);
// Everything we can find for a folder: Explorer windows + processes holding files in its first
// levels (a full scan of a mounted Windows image would take too long).
[[nodiscard]] std::vector<FolderBlocker> blockersOf(const std::filesystem::path& folder);

// Deletes everything inside `folder` (keeps the folder). Leftovers of a broken mount carry
// TrustedInstaller ACLs: on access denied, takes ownership and grants Administrators full
// control, then retries. Never follows reparse points (junctions/symlinks are removed as links).
// Refuses drive roots. Needs admin for the ownership step.
[[nodiscard]] Result<void> forceRemoveContents(const std::filesystem::path& folder);
// Removes one file or one folder with everything in it — the same way (ownership on access
// denied, links removed as links). Something already gone is a success. The caller decides
// what may be removed (SystemComponents::resolveImagePath).
[[nodiscard]] Result<void> forceRemoveEntry(const std::filesystem::path& entry);

} // namespace wl::core
