#pragma once
// D-097: in-place upgrade on an unsupported PC. A clean install off the media already passes the
// checks through boot.wim's LabConfig (BootImage.h); an UPGRADE run from the running Windows does
// not — it reads the *live* host registry before Setup starts, which boot.wim cannot reach. So the
// media carries a small script the user runs in place of setup.exe: it writes the host values Setup
// looks at (what Rufus's setup wrapper writes), then launches the media's setup.exe. It covers a
// missing TPM, no Secure Boot, too little RAM and an unsupported CPU — all the hard checks — and
// keeps the normal "Windows 11 Setup" window, files and apps.
//   HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\HwReqChk  HwReqChkVars (MULTI_SZ)
//     = SQ_SecureBootCapable=TRUE, SQ_SecureBootEnabled=TRUE, SQ_TpmVersion=2, SQ_RamMB=8192
//   HKLM\SYSTEM\Setup\MoSetup  AllowUpgradesWithUnsupportedTPMOrCPU (DWORD) = 1
//   the AppCompatFlags caches of a prior failed check are cleared first.
// POPCNT / SSE4.2 (24H2+) is a hard CPU floor nothing here bypasses.
#include <string>

namespace wl::core {

// The batch file written to the media root as "Yukselt - desteklenmeyen PC.cmd". It elevates
// itself, writes the host values and starts setup.exe next to it. Pure ASCII (a .cmd read by
// cmd.exe in the OEM codepage). Unit-tested.
[[nodiscard]] std::string unsupportedUpgradeCmd();
// The name the file is given on the media.
[[nodiscard]] const wchar_t* unsupportedUpgradeName() noexcept;

} // namespace wl::core
