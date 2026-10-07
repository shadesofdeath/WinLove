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
//     programs.ps1 / .json    D-078: the programs of the Programlar page, installed with winget at the
//     programs-task.xml       first logon in a window of their own ("WinLove Programs" task, registered
//                             by the machine script; it waits for the post-setup task and for a network)
//   Log on the installed system: %ProgramData%\WinLove\postsetup-*.log, programs.log
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
    enum class Type : std::uint8_t { Winget, Command, Copy, Wifi };
    Type type = Type::Command;
    std::wstring name;        // shown in the list, echoed while it runs (Wi-Fi: the SSID)
    std::wstring source;      // winget: package id · command: the command line · copy: file / folder on this PC
                              // · wifi: the WLAN profile XML (Wifi.h; always a machine step, deleted after use)
    std::wstring destination; // copy only: folder on the installed system (%VARIABLES% allowed)
    bool wait = true;         // command only: finish before the next step starts

    [[nodiscard]] bool operator==(const PostSetupStep&) const = default;
};

// D-078: a program of the Programlar page — a winget package, by id, with the name the install
// window shows.
struct PostSetupProgram {
    std::wstring id;
    std::wstring name;

    [[nodiscard]] bool operator==(const PostSetupProgram&) const = default;
};

struct PostSetupPlan {
    // FirstLogon: every step at the first logon (user, elevated). SetupComplete: commands and
    // copies before the first logon (SYSTEM); winget steps still wait for the first logon.
    enum class When : std::uint8_t { FirstLogon, SetupComplete };
    When when = When::FirstLogon;
    bool continueOnError = true;
    std::vector<PostSetupStep> steps;
    // After the steps, at the first logon, in this order (D-078).
    std::vector<PostSetupProgram> programs;
    // The program window's texts in the app's language (keys of programs.ps1's "texts"); a key that
    // is missing keeps its English default.
    std::vector<std::pair<std::wstring, std::wstring>> programTexts;

    [[nodiscard]] bool empty() const noexcept { return steps.empty() && programs.empty(); }

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
    BadWifiProfile,   // not a WLAN profile
};
// (step index, problem) for every step that cannot be written as it is.
[[nodiscard]] std::vector<std::pair<std::size_t, PostSetupProblem>> validatePostSetup(const PostSetupPlan& plan);
// The programs whose id cannot go on a command line (indexes into plan.programs).
[[nodiscard]] std::vector<std::size_t> invalidPrograms(const PostSetupPlan& plan);

// The two batch files ("\r\n" lines). `user` is empty when nothing runs at the first logon.
struct PostSetupScripts {
    std::wstring machine;
    std::wstring user;
};
[[nodiscard]] PostSetupScripts buildPostSetupScripts(const PostSetupPlan& plan);
[[nodiscard]] std::wstring postSetupTaskXml();
// D-078: what the program window reads (UTF-8 JSON: title, texts, programs) and its task.
[[nodiscard]] std::string programsJson(const PostSetupPlan& plan);
[[nodiscard]] std::wstring programsTaskXml();

// Rough run time on the installed system (winget downloads dominate).
[[nodiscard]] double estimatePostSetupSeconds(const PostSetupPlan& plan);
// Bytes the copy steps add to the image.
[[nodiscard]] std::uint64_t postSetupPayloadBytes(const PostSetupPlan& plan);

// Writes scripts, task definition and copy payloads into the mounted image and hooks
// SetupComplete.cmd. Replaces what an earlier run left; an empty plan removes it.
[[nodiscard]] Result<void> applyPostSetup(const std::filesystem::path& mountDir, const PostSetupPlan& plan,
                                          const TaskContext& task);

} // namespace wl::core
