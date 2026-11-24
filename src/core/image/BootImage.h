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
// Drivers go into Setup's image only (the boot index, 2 on Microsoft's media): it is the one the
// media boots and the one Setup looks for disks from; index 1 (plain Windows PE) is not started
// by the media at all (D-134 — a lab VM saw the disk with the driver in index 2 alone).
#include "core/image/DriverInf.h"
#include "core/image/Source.h"
#include "core/image/dism/Dism.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wl::core {

// D-134: a driver that did not go into Setup's image, and why. Checked before DISM is asked where
// that is possible: DISM takes an INF without a models section for the image's architecture as an
// "unsigned" package without a word, and refuses an unsigned storage driver only with 0x80070032
// "the request is not supported" (lab 2026-11-10, ENGINE field notes).
struct DriverRefusal {
    enum class Why : std::uint8_t {
        Missing,        // no such file
        Architecture,   // no models section for the image's architecture (detail: what it has)
        NoCatalog,      // the INF names no catalog: unsigned
        CatalogMissing, // its catalog file is not next to it (detail: the name)
        Unsigned,       // DISM took it but lists it as unsigned (the catalog does not match): taken out again
        Dism,           // DISM refused it (detail: its message)
    };
    std::filesystem::path inf;
    Why why = Why::Dism;
    std::wstring detail;
};
// "pvscsi.inf: built for arm64 only, the setup image is x64" (English, for logs and the CLI).
[[nodiscard]] std::wstring describe(const DriverRefusal& refusal);

// Whether the driver package fits an image of `arch` (Unknown: anything fits). x64 needs an
// NTamd64 models section, ARM64 an NTarm64 one.
[[nodiscard]] bool fitsArchitecture(const DriverInf& inf, Architecture arch);
// What stops `inf` from going into a setup image of `arch` before DISM sees it (nullopt: nothing).
[[nodiscard]] std::optional<DriverRefusal> checkBootDriver(const std::filesystem::path& inf, Architecture arch);

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
    // D-134: each INF of the patch ends up in exactly one of these three.
    std::vector<std::filesystem::path> driversAdded;   // in the image now, DISM lists them signed
    std::vector<std::filesystem::path> driversPresent; // the image had that package already (DISM added nothing)
    std::vector<DriverRefusal> driversRefused;         // not in the image; the rest still went in
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
