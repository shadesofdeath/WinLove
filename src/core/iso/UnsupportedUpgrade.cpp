#include "core/iso/UnsupportedUpgrade.h"

namespace wl::core {

const wchar_t* unsupportedUpgradeName() noexcept {
    return L"Yukselt - desteklenmeyen PC.cmd";
}

std::string unsupportedUpgradeCmd() {
    // ASCII only: cmd.exe reads a .cmd in the console's OEM codepage. The REG_MULTI_SZ separator
    // reg.exe uses is \0 inside the /d value. The script self-elevates (HKLM writes need admin),
    // then launches the media's setup.exe from its own folder.
    return
        "@echo off\r\n"
        "setlocal\r\n"
        "net session >nul 2>&1\r\n"
        "if %errorlevel% neq 0 (\r\n"
        "  powershell -NoProfile -Command \"Start-Process -Verb RunAs -FilePath '%~f0'\"\r\n"
        "  exit /b\r\n"
        ")\r\n"
        "echo Preparing this PC for an in-place upgrade (unsupported hardware)...\r\n"
        "set \"ACF=HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\"\r\n"
        "reg delete \"%ACF%\\CompatMarkers\" /f >nul 2>&1\r\n"
        "reg delete \"%ACF%\\Shared\" /f >nul 2>&1\r\n"
        "reg delete \"%ACF%\\TargetVersionUpgradeExperienceIndicators\" /f >nul 2>&1\r\n"
        "reg add \"%ACF%\\HwReqChk\" /v HwReqChkVars /t REG_MULTI_SZ /d "
        "\"SQ_SecureBootCapable=TRUE\\0SQ_SecureBootEnabled=TRUE\\0SQ_TpmVersion=2\\0SQ_RamMB=8192\" /f >nul 2>&1\r\n"
        "reg add \"HKLM\\SYSTEM\\Setup\\MoSetup\" /v AllowUpgradesWithUnsupportedTPMOrCPU /t REG_DWORD /d 1 /f >nul 2>&1\r\n"
        "if not exist \"%~dp0setup.exe\" (\r\n"
        "  echo setup.exe not found next to this script.\r\n"
        "  pause\r\n"
        "  exit /b 1\r\n"
        ")\r\n"
        "echo Starting Windows Setup...\r\n"
        "start \"\" \"%~dp0setup.exe\"\r\n"
        "endlocal\r\n";
}

} // namespace wl::core
