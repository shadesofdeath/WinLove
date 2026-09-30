#pragma once
// Setup's own image: sources\boot.wim, the index the media boots (Windows PE with Setup in it).
// Two things only belong there:
//   - the Windows 11 hardware checks read HKLM\SYSTEM\Setup\LabConfig of the *running* Windows PE.
//     Written into boot.wim's SYSTEM hive the bypass holds whatever answer file is (or is not) on
//     the media; autounattend.xml does the same through a command that must run first.
//   - drivers Setup itself needs: a storage controller it cannot see (Intel VMD / RST), a network
//     adapter for the online steps.
// patchBootImage mounts that index in a folder of its own, applies the patch and commits.
// Needs an elevated process; the install image may be mounted elsewhere meanwhile.
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
    std::vector<std::filesystem::path> drivers; // .inf files

    [[nodiscard]] bool empty() const noexcept {
        return !bypassTpm && !bypassSecureBoot && !bypassRam && !bypassCpu && !bypassStorage && drivers.empty();
    }
    // "BypassTPMCheck" … for the checks that are on.
    [[nodiscard]] std::vector<std::wstring> labConfigValues() const;
};

struct BootPatchReport {
    int index = 0;                             // the image that was patched
    std::size_t driversAdded = 0;
    std::vector<std::wstring> driversRefused;  // "<inf>: <why>" — the rest still went in
};

// The index Setup boots from: the header's boot index, else the last image.
[[nodiscard]] Result<int> setupImageIndex(const std::filesystem::path& bootWim);

// Nothing to do (`patch.empty()`) is a success without a mount. Whatever fails after the mount,
// the image is unmounted without saving and the file stays as it was.
[[nodiscard]] Result<BootPatchReport> patchBootImage(Dism& dism, const std::filesystem::path& bootWim,
                                                     const std::filesystem::path& mountDir, const BootPatch& patch,
                                                     const TaskContext& task);

} // namespace wl::core
