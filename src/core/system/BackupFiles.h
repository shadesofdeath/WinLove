#pragma once
// Listing and measuring folders of a mounted image the way the engine needs it: through
// FILE_FLAG_BACKUP_SEMANTICS (Program Files\WindowsApps, WinSxS … are closed to administrators;
// SeBackupPrivilege must be enabled), with GetFileInformationByHandleEx instead of
// std::filesystem (directory_iterator stops at the first entry it cannot stat). Reparse-point
// folders (junctions, symlinks) are never entered; a mounted WIM's own reparse files are files.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wl::core {

// Sum of file sizes under `folder`, readable even where the ACL denies administrators.
[[nodiscard]] std::uint64_t backupFolderSize(const std::filesystem::path& folder);
// Names of the direct subfolders (same access rules).
[[nodiscard]] std::vector<std::wstring> backupListFolders(const std::filesystem::path& folder);
// Names of the direct files (same access rules).
[[nodiscard]] std::vector<std::wstring> backupListFiles(const std::filesystem::path& folder);
// The bytes of a file (same access rules); nothing when it cannot be read or is larger than `limit`.
[[nodiscard]] std::optional<std::string> backupReadFile(const std::filesystem::path& file, std::size_t limit);

} // namespace wl::core
