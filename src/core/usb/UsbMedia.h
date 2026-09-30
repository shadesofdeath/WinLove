#pragma once
// D-047: a bootable Windows setup stick from a setup folder — the USB tab of P06.
// Microsoft's documented way (WinPE / setup media): diskpart cleans the disk, makes one FAT32
// partition (MBR + active for BIOS and UEFI, or GPT for UEFI only), formats it; bootsect /nt60
// from the media writes BOOTMGR's boot code; the files are copied. FAT32 holds files up to 4 GB:
// a larger install.wim is split into install.swm, install2.swm … which Setup reads on its own
// (install.esd larger than 4 GB cannot be split — the ISO tab's repack to WIM is the way).
// Windows' FAT32 formatter stops at 32 GB, so a larger stick gets a 32 GB partition.
//
// Safety: only disks on the USB (or SD / MMC) bus are listed, never the disk Windows runs from or
// a disk holding the page file; the write re-reads the disk right before diskpart runs and stops
// unless its bus, size, model and serial are still what the user picked. Needs an elevated process.
#include "base/Result.h"
#include "core/iso/IsoBuilder.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct UsbDisk {
    int number = -1;            // \\.\PhysicalDriveN
    std::wstring vendor;
    std::wstring model;
    std::wstring serial;
    std::uint64_t size = 0;
    std::uint32_t busType = 0;  // STORAGE_BUS_TYPE
    bool removableMedia = false;
    bool system = false;        // holds the Windows or boot volume / page file: never offered
    std::vector<std::wstring> letters; // "E:\" …

    [[nodiscard]] std::wstring name() const;     // "SanDisk Ultra" (vendor + model, trimmed)
    [[nodiscard]] std::wstring identity() const; // what must not change between pick and write
};

// Disks a setup stick can go to. `includeVirtual`: also attached VHD(X)s — only for lab tests
// (wlcli --allow-virtual), never in the app. No admin needed.
[[nodiscard]] std::vector<UsbDisk> listUsbDisks(bool includeVirtual = false);
// Diagnosis only (wlcli usb-list --all): every disk, any bus, the system disk too (marked).
// Nothing writes to what this returns.
[[nodiscard]] std::vector<UsbDisk> listAllDisks();

enum class UsbScheme : std::uint8_t { MbrBiosUefi, GptUefi };

struct UsbOptions {
    int disk = -1;
    std::wstring identity;  // UsbDisk::identity() as it was picked
    UsbScheme scheme = UsbScheme::MbrBiosUefi;
    std::wstring label;     // FAT32 label (fatLabel() makes it valid)
    std::filesystem::path sourceFolder;
    std::vector<IsoOptions::RootFile> rootFiles;         // autounattend.xml …
    std::vector<IsoOptions::ReplacedFile> replacedFiles; // patched boot.wim …
    bool allowVirtual = false;
};

struct UsbResult {
    std::wstring root;  // "E:\"
    std::uint64_t bytes = 0;
    int swmParts = 0;   // 0: install.wim copied as it is
};

// Pure pieces (unit-tested).
inline constexpr std::uint64_t kFat32FileLimit = 0xFFFFFFFFull;          // 4 GiB - 1
inline constexpr std::uint64_t kSwmPartSize = 3800ull * 1024 * 1024;      // well under the limit
inline constexpr std::uint64_t kFat32FormatLimit = 32000ull * 1024 * 1024; // diskpart / format.com FAT32 limit
[[nodiscard]] std::wstring fatLabel(std::wstring_view label);             // ≤ 11, A–Z 0–9 _ -
// Size of the partition in MB (0 = the whole disk).
[[nodiscard]] std::uint64_t usbPartitionMb(std::uint64_t diskBytes);
[[nodiscard]] std::string diskpartScript(int disk, UsbScheme scheme, std::uint64_t partitionMb, std::wstring_view label);
// Bytes the stick needs for `folder` (+ root files, replacements); files over 4 GB other than
// install.wim make it impossible (the error names the file).
struct UsbPlan {
    std::uint64_t bytes = 0;
    bool splitInstall = false;
    std::filesystem::path installWim;
};
[[nodiscard]] Result<UsbPlan> planUsbCopy(const UsbOptions& options);

[[nodiscard]] Result<UsbResult> writeUsb(const UsbOptions& options, const TaskContext& task);

} // namespace wl::core
