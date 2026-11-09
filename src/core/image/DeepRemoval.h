#pragma once
// P07 deep removal (D-060): inbox drivers Windows only ships inside its core packages (modems, tape,
// floppy, FireWire …) cannot be removed as a package. Deep removal takes a driver package out of
// every place the image keeps it, so that the component store stays consistent:
//
//   COMPONENTS  DerivedData\Components\<arch>_dual_<inf>_…   (the component)
//               CanonicalData\Deployments\dual_<inf>_…        (its own deployment)
//   WinSxS      <arch>_dual_<inf>_…\ (payload), Manifests\<arch>_dual_<inf>_….manifest
//   DriverStore FileRepository\<inf>_<arch>_<hash>\, <culture>\<inf>_loc
//   DRIVERS     DriverDatabase\DriverPackages\<package>, DriverInfFiles\<inf>, DeviceIds\*\<inf>,
//               DriverFiles\<file> (when this INF is its only owner)
//   Windows\INF <inf>, <name>.pnf
//   elsewhere   every other hard link of a payload file (System32\drivers\x.sys …), and the
//               SYSTEM services whose ImagePath is such a file
//
// The packages that list these drivers stay installed: CBS no longer finds the driver's deployment
// and treats it as never installed. /ScanHealth must stay clean (tools\lab_deep_removal.ps1).
// What is not undone: a later cumulative update or /RestoreHealth may bring a driver back.
//
// Only device classes on a fixed list can be targeted (kDeepRemovableClasses): a preset file must
// never be able to take network, storage, USB, input or display drivers out of an image. Class
// definition INFs (c_*.inf) are kept.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

// Upper-case "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" class GUIDs deep removal may target.
[[nodiscard]] bool isDeepRemovableClass(std::wstring_view classGuid);
// "{4d36e96d-e325-11ce-bfc1-08002be10318}" → upper case; empty when it is not a GUID.
[[nodiscard]] std::wstring normalizeClassGuid(std::wstring_view text);
// The class GUID in a DriverPackages\<package> "Version" value (bytes 8..23, little-endian GUID).
[[nodiscard]] std::wstring driverPackageClass(const std::vector<std::uint8_t>& version);

struct InboxDriver {
    std::wstring package; // FileRepository folder / DriverPackages key: "mdm3com.inf_amd64_60545ab840205b89"
    std::wstring inf;     // "mdm3com.inf"
    std::wstring classGuid;
};
// The image's driver packages of these classes (DRIVERS hive, read through offreg), class INFs left out.
[[nodiscard]] Result<std::vector<InboxDriver>> findInboxDrivers(const std::filesystem::path& mountDir,
                                                                const std::vector<std::wstring>& classGuids);

// D-112: these driver packages (DriverPackages key names), with their INF and class; packages the
// image does not have are left out.
[[nodiscard]] Result<std::vector<InboxDriver>> findDriverPackages(const std::filesystem::path& mountDir,
                                                                  const std::vector<std::wstring>& packages);

struct DeepRemovalResult {
    std::size_t drivers = 0;
    std::size_t files = 0;      // files and folders removed
    std::size_t registry = 0;   // keys and values removed
    std::uint64_t bytes = 0;    // WinSxS payload removed
};
// Takes the drivers out (see above). Nothing else may hold the image's hives (the caller closes the
// DISM session). Elevated process.
[[nodiscard]] Result<DeepRemovalResult> deepRemoveDrivers(const std::filesystem::path& mountDir,
                                                          const std::vector<InboxDriver>& drivers, const TaskContext& task);

// D-113: the installed copies of store components (identity names, lower case; bits 0 all, 32 only
// x86 / wow64, 64 only native): every hard link of their WinSxS payload outside WinSxS, and these
// services' keys (every control set). The store and its registration stay — the reference tool's custom removal
// with update support kept: a later cumulative update or sfc /scannow can put the files back.
// What Windows loads at boot, the device filters (with their service keys) and every module these
// and the kernel import stay (D-116 / D-117, BootDrivers.h), and so does every module one that stays
// imports or delay-loads (D-118). Elevated; nothing else may hold the SYSTEM hive (the caller closes
// the DISM session).
struct StoreRemovalResult {
    std::size_t components = 0;  // store components found
    std::size_t files = 0;       // installed copies removed
    std::size_t services = 0;    // service keys removed
    std::size_t keptDrivers = 0; // boot files kept: drivers, filters, the loader's and their imports (D-116 / D-117)
    std::size_t keptImports = 0; // modules kept because one that stays imports or delay-loads them (D-118)
};
[[nodiscard]] Result<StoreRemovalResult> removeStoreCopies(const std::filesystem::path& mountDir,
                                                           const std::vector<std::wstring>& names, std::uint8_t bits,
                                                           const std::vector<std::wstring>& services, const TaskContext& task);

} // namespace wl::core
