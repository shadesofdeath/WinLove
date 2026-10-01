#pragma once
// Listing and measuring folders of a mounted image the way the engine needs it: through
// FILE_FLAG_BACKUP_SEMANTICS (Program Files\WindowsApps, WinSxS … are closed to administrators;
// SeBackupPrivilege must be enabled), with GetFileInformationByHandleEx instead of
// std::filesystem (directory_iterator stops at the first entry it cannot stat). Reparse-point
// folders (junctions, symlinks) are never entered; a mounted WIM's own reparse files are files.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

// Sum of file sizes under `folder`, readable even where the ACL denies administrators.
[[nodiscard]] std::uint64_t backupFolderSize(const std::filesystem::path& folder);
// Names of the direct subfolders (same access rules).
[[nodiscard]] std::vector<std::wstring> backupListFolders(const std::filesystem::path& folder);
// Names of the direct files (same access rules).
[[nodiscard]] std::vector<std::wstring> backupListFiles(const std::filesystem::path& folder);

} // namespace wl::core
