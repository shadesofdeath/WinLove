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

#include <cstdint>
#include <filesystem>
#include <optional>
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

// ---- D-102: custom recurring tasks the image creates after setup ------------------------------
// Windows has no such task until we make one; SetupComplete.cmd registers it (SYSTEM, highest) with
//   schtasks /Create /TN "\WinLove\<name>" /TR "<command>" /SC <...> [/ST HH:MM] [/D MON] /RU SYSTEM
//   /RL HIGHEST /F
// written to <image>\Windows\Setup\Scripts\WinLove\taskcreate.cmd (its JSON kept in taskcreate.json
// so the page can read it back). A created task is its own thing — the disable list (tasks.cmd) is
// untouched.
inline constexpr wchar_t kCreateTasksScript[] = L"taskcreate.cmd";
inline constexpr wchar_t kCreateTasksData[] = L"taskcreate.json";

struct CreatedTask {
    enum class Trigger : std::uint8_t { AtLogon, AtStartup, Daily, Weekly, Hourly };
    std::wstring name;              // shown and used as \WinLove\<name>
    Trigger trigger = Trigger::Daily;
    std::wstring time;              // "HH:MM" for Daily / Weekly (empty = schtasks default)
    int weekday = 0;               // Weekly only: 0=Mon … 6=Sun
    std::wstring command;          // the command line the task runs

    [[nodiscard]] bool operator==(const CreatedTask&) const = default;
};

enum class CreatedTaskProblem : std::uint8_t { EmptyName, BadName, EmptyCommand, BadCommand, BadTime };
// Why this task cannot be written, or empty when it can. Name: no quotes/backslashes/control/cmd
// metacharacters, 1..200. Command: non-empty, no quotes or control characters. Time: HH:MM 24h.
[[nodiscard]] std::optional<CreatedTaskProblem> validCreatedTask(const CreatedTask& task);

// One `schtasks /Create …` line for the task (no redirection; the script adds it).
[[nodiscard]] std::wstring createTaskCommand(const CreatedTask& task);
// The taskcreate.cmd body for these tasks (no header: writeSetupScript adds it).
[[nodiscard]] std::wstring createdTasksScriptBody(const std::vector<CreatedTask>& tasks);

[[nodiscard]] std::string createdTasksToJson(const std::vector<CreatedTask>& tasks);
[[nodiscard]] Result<std::vector<CreatedTask>> createdTasksFromJson(std::string_view json);

// The tasks the image is set to create (read from taskcreate.json; empty when none).
[[nodiscard]] std::vector<CreatedTask> readCreatedTasks(const std::filesystem::path& mountDir);
// Writes taskcreate.cmd (+ taskcreate.json) for exactly `tasks`; an empty list removes both.
[[nodiscard]] Result<void> setCreatedTasks(const std::filesystem::path& mountDir, const std::vector<CreatedTask>& tasks);

} // namespace wl::core
