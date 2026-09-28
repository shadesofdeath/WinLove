#pragma once
// P07 data: provisioned AppX packages of a mounted image with their on-disk size. Sizes come from
// <mount>\Program Files\WindowsApps\<Name>_* (the main package plus its resource/arch packages).
// That folder is locked down by ACL even for administrators, so it is walked with backup semantics
// (SeBackupPrivilege) — no ownership changes, read-only.
#include "core/image/dism/Dism.h"

#include <vector>

namespace wl::core {

struct AppxComponent {
    AppxEntry package;
    std::uint64_t size = 0; // 0 = unknown
};

[[nodiscard]] Result<std::vector<AppxComponent>> readAppx(Dism& dism, const std::filesystem::path& mountDir,
                                                          const TaskContext& task);

// Sum of file sizes under `folder`, readable even where the ACL denies administrators
// (opens with FILE_FLAG_BACKUP_SEMANTICS; SeBackupPrivilege must be enabled — done here).
[[nodiscard]] std::uint64_t backupFolderSize(const std::filesystem::path& folder);
// Names of the direct subfolders (same access rules).
[[nodiscard]] std::vector<std::wstring> backupListFolders(const std::filesystem::path& folder);

} // namespace wl::core
