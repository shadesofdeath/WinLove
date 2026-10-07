#pragma once
// Windows' own dism.exe on a mounted image, for what the DISM API has no call for: component
// store cleanup (StoreCleanup.h) and editions (Edition.h). It opens its own session on the image,
// so ours is suspended while it runs (DismSession::suspend / reload).
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

class DismSession;

// <System32>\dism.exe
[[nodiscard]] Result<std::filesystem::path> dismExePath();
// "<dism.exe>" /English /Image:"<mount>" <arguments>. /English: the output is parsed and logged.
[[nodiscard]] std::wstring dismExeCommandLine(const std::filesystem::path& dismExe, const std::filesystem::path& mountDir,
                                              std::wstring_view arguments);
// The last percentage in a piece of dism.exe console output ("[=====   20.0%   ]"), 0 … 1.
[[nodiscard]] std::optional<double> lastDismPercent(std::string_view output);

struct DismExeRun {
    std::uint32_t exitCode = 0;
    std::string output;   // everything printed (capped: the end is what matters when it fails)
    std::wstring message; // from its "Error: …" line on, in one line; empty when it printed none
};
// Runs dism.exe on the image of `session` (suspended meanwhile, reopened afterwards) and logs what
// it says. `onPercent` gets its progress bar. An error only when it could not run, or the session
// could not be reopened; a failed command is an `exitCode`.
// Every "<label> : <value>" of /English output ("Current Edition : Core", "System locale : tr-TR").
[[nodiscard]] std::vector<std::wstring> dismExeValues(std::string_view output, std::string_view label);
// A failed run as an Error: DISM's own message, or the exit code.
[[nodiscard]] Error dismExeFailure(const DismExeRun& run, std::wstring message);

[[nodiscard]] Result<DismExeRun> runDismExe(DismSession& session, std::wstring_view arguments,
                                            const std::function<void(double)>& onPercent = {});

// D-063: a package through the DISM API; a UUP-based .msu (24H2+ cumulative update) the API refuses
// on a host whose servicing stack moved on ("Active offline session not registered", 0x800401E3)
// goes in through dism.exe /Add-Package, which installs the same file into the same image.
[[nodiscard]] Result<void> addPackageOrDismExe(DismSession& session, const std::filesystem::path& package,
                                               const TaskContext& task);

} // namespace wl::core
