#pragma once
// Elevation and token privileges. WinLove starts asInvoker (D-005); image operations need an
// elevated process, offline registry hives need SeBackup/SeRestore.
#include "base/Result.h"

#include <string>
#include <string_view>

namespace wl::core {

[[nodiscard]] bool isElevated() noexcept;

// Enables a privilege (SE_BACKUP_NAME, SE_RESTORE_NAME, ...) on the process token.
[[nodiscard]] Result<void> enablePrivilege(const wchar_t* name);

// Starts this executable again through UAC ("runas") with `arguments`. The caller exits on success.
// ErrorCode::Cancelled when the user declines the UAC prompt.
[[nodiscard]] Result<void> relaunchElevated(std::wstring_view arguments);

} // namespace wl::core
