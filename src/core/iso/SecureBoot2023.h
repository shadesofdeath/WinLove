#pragma once
// D-094: setup media that boots with the boot manager signed by "Windows UEFI CA 2023" instead of
// "Microsoft Windows Production PCA 2011" (KB5053484 / KB5025885, CVE-2023-24932): once a PC's
// firmware revokes the 2011 CA (DBX), only such media starts there; a PC whose db lacks the 2023 CA
// starts only the old one. What Microsoft's Make2023BootableMedia.ps1 does, without a mount:
//   from sources\boot.wim, image 1 (a 2024-04 or later cumulative update put them there):
//     Windows\Boot\EFI_EX\bootmgfw_EX.efi            → efi\boot\bootx64.efi (bootaa64.efi on ARM64)
//     Windows\Boot\EFI_EX\bootmgr_EX.efi             → bootmgr.efi
//     Windows\Boot\DVD_EX\EFI\en-US\efisys_EX.bin    → the UEFI El Torito image (efi\microsoft\boot\efisys_ex.bin)
//       (efisys_noprompt_EX.bin for "no Press any key")
//     Windows\Boot\Fonts_EX\<name>_EX.ttf            → efi\microsoft\boot\fonts\<name>.ttf
//     Windows\Boot\EFI\boot.stl                      → efi\microsoft\boot\boot.stl
// BIOS boot (etfsboot.com, bootmgr), the BCD and boot.wim stay as they are. The files go into the
// ISO in place of the folder's (IsoOptions::replacedFiles): the setup folder itself is not changed.
#include "core/iso/IsoBuilder.h"

#include <filesystem>
#include <optional>

namespace wl::core {

struct SecureBoot2023 {
    std::vector<IsoOptions::ReplacedFile> files; // for IsoOptions::replacedFiles
    std::filesystem::path efiBootImage;          // for IsoOptions::efiBootImage
    std::wstring bootManagerVersion;             // of bootmgfw_EX.efi ("10.0.26100.6584")
};

// Reads the 2023-signed boot files out of `bootWim` (empty: <media>\sources\boot.wim; the copy the
// ISO will carry when it was patched or updated — the newest boot manager is the one to take) into
// `scratch` (kept until the ISO is written). NotFound naming the update to add when it has none.
[[nodiscard]] Result<SecureBoot2023> prepareSecureBoot2023(const std::filesystem::path& media,
                                                           const std::filesystem::path& bootWim,
                                                           const std::filesystem::path& scratch, bool noPrompt);

// Does this PC's UEFI db hold the "Windows UEFI CA 2023" certificate? nullopt: not known (BIOS
// boot, Secure Boot variables not readable — needs an elevated process).
[[nodiscard]] std::optional<bool> firmwareTrustsCa2023();
// Pure part: the db variable's bytes (EFI signature lists of X.509 certificates).
[[nodiscard]] bool dbHasCa2023(std::span<const std::byte> db);

} // namespace wl::core
