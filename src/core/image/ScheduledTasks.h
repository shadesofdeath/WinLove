#pragma once
// D-048: scheduled tasks switched off on the installed system.
// An offline image has (almost) no tasks yet — Windows registers them during setup, from the
// component manifests — and editing a registered task's XML is caught by the Task Scheduler (the
// TaskCache keeps a hash of it: the task turns "corrupt"). So the change is made where Windows
// itself makes it: SetupComplete.cmd runs `schtasks /Change /TN <path> /Disable` for each task
// (SYSTEM, after OOBE; the tasks exist by then). A task the edition does not have is only a line
// in the log (%ProgramData%\WinLove\tasks.log). The script, one line per task:
//   <image>\Windows\Setup\Scripts\WinLove\tasks.cmd
// Windows Update may register a task again later (the Compatibility Appraiser does): switching
// it off holds until then.
#include "base/Result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

inline constexpr wchar_t kTasksScript[] = L"tasks.cmd";

// "\Microsoft\Windows\Autochk\Proxy": starts with a backslash, no empty or "."/".." parts, no
// quotes, control characters or characters cmd would act on (% ^ & | < > "), at most 260.
[[nodiscard]] bool validTaskPath(std::wstring_view path);

// The tasks a tasks.cmd switches off (its own lines only; case kept as written).
[[nodiscard]] std::vector<std::wstring> disabledTasksIn(std::wstring_view script);
// The script body for these tasks (no header: writeSetupScript adds it).
[[nodiscard]] std::wstring tasksScriptBody(const std::vector<std::wstring>& paths);

// What the image's tasks.cmd switches off (empty: no script).
[[nodiscard]] std::vector<std::wstring> readDisabledTasks(const std::filesystem::path& mountDir);
// Adds the task to the script or takes it out (case-insensitive); the last one out deletes it.
[[nodiscard]] Result<void> setTaskDisabled(const std::filesystem::path& mountDir, std::wstring_view path, bool disabled);

} // namespace wl::core
