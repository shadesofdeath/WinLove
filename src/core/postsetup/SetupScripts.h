#pragma once
// Small scripts of their own that SetupComplete.cmd calls on the installed system (SYSTEM, after
// OOBE, before the first logon): scheduled tasks switched off (D-048), DNS servers (D-049).
// They live next to the post-setup scripts, in <image>\Windows\Setup\Scripts\WinLove\<name>, and
// the SetupComplete line checks for the file before calling it — removing the file is enough to
// take a script back. Batch files are UTF-8 with "chcp 65001" first; PowerShell scripts UTF-8
// with a BOM (Windows PowerShell reads a BOM-less script as ANSI).
// Caveat (D-026): Windows skips SetupComplete.cmd when it is activated with an OEM product key.
#include "base/Result.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace wl::core {

[[nodiscard]] std::filesystem::path setupScriptPath(const std::filesystem::path& mountDir, std::wstring_view name);

// The SetupComplete.cmd line for a script ("if exist … call …" / "… powershell -File …").
[[nodiscard]] std::string setupScriptCallLine(std::wstring_view name);

// Writes the script (`body` without the header lines) and hooks it into SetupComplete.cmd.
[[nodiscard]] Result<void> writeSetupScript(const std::filesystem::path& mountDir, std::wstring_view name,
                                            std::wstring_view body, std::string_view comment);
// Deletes the script (the hook line stays and finds nothing). Missing = success.
[[nodiscard]] Result<void> removeSetupScript(const std::filesystem::path& mountDir, std::wstring_view name);
// The script as it is in the image (without its BOM); empty when there is none.
[[nodiscard]] std::wstring readSetupScript(const std::filesystem::path& mountDir, std::wstring_view name);

} // namespace wl::core
