#pragma once
// P14: steps that run on the installed system once setup is over — install an app with winget,
// run a command, copy files. Same mechanism and folder as D-026 (the NTLite way):
//
//   <image>\Windows\Setup\Scripts\WinLove\
//     postsetup-machine.cmd   called from SetupComplete.cmd (SYSTEM, after OOBE, before the first
//                             logon): runs the machine steps and registers the logon task
//     postsetup-user.cmd      run once by that task at the first logon, as the user who logs on,
//                             with the highest privileges the account has (winget needs a user)
//     postsetup-task.xml      the task definition (principal = BUILTIN\Users by SID, so it does not
//                             depend on the display language)
//     files\<n>\…             what copy step n carries into the image
//   Log on the installed system: %ProgramData%\WinLove\postsetup.log
//
// Caveat (D-026): Windows skips SetupComplete.cmd when it is activated with an OEM product key.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wl::core {

struct PostSetupStep {
    enum class Type : std::uint8_t { Winget, Command, Copy };
    Type type = Type::Command;
    std::wstring name;        // shown in the list, echoed while it runs
    std::wstring source;      // winget: package id · command: the command line · copy: file / folder on this PC
    std::wstring destination; // copy only: folder on the installed system (%VARIABLES% allowed)
    bool wait = true;         // command only: finish before the next step starts

    [[nodiscard]] bool operator==(const PostSetupStep&) const = default;
};

struct PostSetupPlan {
    // FirstLogon: every step at the first logon (user, elevated). SetupComplete: commands and
    // copies before the first logon (SYSTEM); winget steps still wait for the first logon.
    enum class When : std::uint8_t { FirstLogon, SetupComplete };
    When when = When::FirstLogon;
    bool continueOnError = true;
    std::vector<PostSetupStep> steps;

    [[nodiscard]] bool operator==(const PostSetupPlan&) const = default;
};

// The plan as the value of one ChangeSet operation (and so of a preset).
[[nodiscard]] std::string postSetupToJson(const PostSetupPlan& plan);
[[nodiscard]] Result<PostSetupPlan> postSetupFromJson(std::string_view json);

enum class PostSetupProblem : std::uint8_t {
    EmptySource,      // no id / command / path
    BadWingetId,      // something other than letters, digits and . - _ +
    EmptyDestination, // copy without a target folder
    BadDestination,   // a quote in the target folder
};
// (step index, problem) for every step that cannot be written as it is.
[[nodiscard]] std::vector<std::pair<std::size_t, PostSetupProblem>> validatePostSetup(const PostSetupPlan& plan);

// The two batch files ("\r\n" lines). `user` is empty when nothing runs at the first logon.
struct PostSetupScripts {
    std::wstring machine;
    std::wstring user;
};
[[nodiscard]] PostSetupScripts buildPostSetupScripts(const PostSetupPlan& plan);
[[nodiscard]] std::wstring postSetupTaskXml();

// Rough run time on the installed system (winget downloads dominate).
[[nodiscard]] double estimatePostSetupSeconds(const PostSetupPlan& plan);
// Bytes the copy steps add to the image.
[[nodiscard]] std::uint64_t postSetupPayloadBytes(const PostSetupPlan& plan);

// Writes scripts, task definition and copy payloads into the mounted image and hooks
// SetupComplete.cmd. Replaces what an earlier run left; an empty plan removes it.
[[nodiscard]] Result<void> applyPostSetup(const std::filesystem::path& mountDir, const PostSetupPlan& plan,
                                          const TaskContext& task);

} // namespace wl::core
