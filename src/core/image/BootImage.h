#pragma once
// Setup's own image: sources\boot.wim, the index the media boots (Windows PE with Setup in it).
// Two things only belong there:
//   - the Windows 11 hardware checks read HKLM\SYSTEM\Setup\LabConfig of the *running* Windows PE.
//     Written into boot.wim's SYSTEM hive the bypass holds whatever answer file is (or is not) on
//     the media; autounattend.xml does the same through a command that must run first.
//   - drivers Setup itself needs: a storage controller it cannot see (Intel VMD / RST), a network
//     adapter for the online steps.
//   - which Setup starts (D-074). Since 24H2 winpeshl.exe (HKLM\SYSTEM\Setup\CmdLine) runs X:\setup.exe,
//     the new Setup, which needs the install image's WinRE; "Previous version of Setup" is
//     X:\sources\setup.exe. CmdLine pointed there (with wpeinit, which winpeshl would have run) makes
//     the previous one the only one.
// patchBootImage mounts that index in a folder of its own, applies the patch and commits.
// Needs an elevated process; the install image may be mounted elsewhere meanwhile.
#include "core/image/Source.h"
#include "core/image/dism/Dism.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct BootPatch {
    bool bypassTpm = false;
    bool bypassSecureBoot = false;
    bool bypassRam = false;
    bool bypassCpu = false;
    bool bypassStorage = false;
    bool legacySetup = false;                   // boot into the previous Setup (24H2+)
    std::vector<std::filesystem::path> drivers; // .inf files
    // D-080: the latest cumulative update for Setup's own images — every index of boot.wim, as
    // Microsoft's media refresh does (steps 17, 23–25). The files the media must then take from the
    // updated Setup image (its whole sources\ folder, the boot manager) are copied to `setupFilesTo`.
    std::filesystem::path lcu;
    std::filesystem::path setupFilesTo;

    [[nodiscard]] bool empty() const noexcept {
        return !bypassTpm && !bypassSecureBoot && !bypassRam && !bypassCpu && !bypassStorage && !legacySetup &&
               drivers.empty() && lcu.empty();
    }
    // "BypassTPMCheck" … for the checks that are on.
    [[nodiscard]] std::vector<std::wstring> labConfigValues() const;
};

// What `legacySetup` writes into HKLM\SYSTEM\Setup\CmdLine (the form tested on real hardware and
// multiboot sticks: wpeinit brings up PnP / network, Setup does not wait for it).
inline constexpr std::wstring_view kLegacySetupCmdLine = L"cmd /c start /min wpeinit && \\sources\\setup";

struct BootPatchReport {
    int index = 0;                             // the image that was patched (Setup's)
    int updated = 0;                           // images the cumulative update went into
    std::wstring versionBefore, versionAfter;  // Setup's image, "10.0.26100.1742"
    std::size_t driversAdded = 0;
    std::vector<std::wstring> driversRefused;  // "<inf>: <why>" — the rest still went in
};

// The editions of a 24H2+ (build 26100+) install image without Windows\System32\Recovery\Winre.wim:
// the new Setup stops early on them (~5 %), the previous one installs them (D-074). Read from the
// file lists, no mount (~0.2 s per edition). Unsupported for ESD.
[[nodiscard]] Result<std::vector<int>> editionsWithoutWinre(const SourceInfo& source);

// The index Setup boots from: the header's boot index, else the last image.
[[nodiscard]] Result<int> setupImageIndex(const std::filesystem::path& bootWim);

// Nothing to do (`patch.empty()`) is a success without a mount. Whatever fails after the mount,
// the image is unmounted without saving and the file stays as it was.
[[nodiscard]] Result<BootPatchReport> patchBootImage(Dism& dism, const std::filesystem::path& bootWim,
                                                     const std::filesystem::path& mountDir, const BootPatch& patch,
                                                     const TaskContext& task);

} // namespace wl::core
