#pragma once
// Windows' own dism.exe on a mounted image, for what the DISM API has no call for: component
// store cleanup (StoreCleanup.h) and editions (Edition.h). It opens its own session on the image,
// so ours is suspended while it runs (DismSession::suspend / reload).
#include "base/Result.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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
[[nodiscard]] Result<DismExeRun> runDismExe(DismSession& session, std::wstring_view arguments,
                                            const std::function<void(double)>& onPercent = {});

} // namespace wl::core
