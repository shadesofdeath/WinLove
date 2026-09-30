#pragma once
// Bootable Windows setup ISO from a setup folder (P06), with Windows' own IMAPI2FS (no ADK /
// oscdimg): UDF 1.02 (install.wim > 4 GB), El Torito boot catalog with BIOS (boot\etfsboot.com,
// no emulation) and/or UEFI (efi\microsoft\boot\efisys.bin, platform 0xEF). The image stream is
// produced lazily by IMAPI while we copy it to the file, so building = writing (progress, cancel).
// Optional: SHA-256 of the result (CNG). Engine thread only.
#include "core/image/WimFile.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

enum class BootMode : std::uint8_t { UefiAndBios, UefiOnly, BiosOnly };

struct IsoOptions {
    std::filesystem::path sourceFolder;  // extracted setup media (boot\, efi\, sources\ …)
    std::filesystem::path output;        // .iso path; replaced if it exists
    std::wstring volumeLabel;            // UDF volume identifier (≤ 32 chars used)
    BootMode boot = BootMode::UefiAndBios;
    bool noPrompt = false;               // efisys_noprompt.bin: no "Press any key to boot from CD"
    bool writeSha256 = false;            // <output>.sha256 next to the ISO
    // Files written to the image root from memory (autounattend.xml, P13): the source folder is
    // not touched; a file of the same name in it is replaced in the image.
    struct RootFile {
        std::wstring name;
        std::string content;
    };
    std::vector<RootFile> rootFiles;
    // Files of the source folder that go into the image from somewhere else (sources\boot.wim
    // patched in a copy, D-038): the folder's own file is left out and stays as it is.
    struct ReplacedFile {
        std::wstring path;          // in the image, from its root: L"sources\\boot.wim"
        std::filesystem::path file; // read while the image is written: must stay until buildIso returns
    };
    std::vector<ReplacedFile> replacedFiles;
};

struct IsoResult {
    std::uint64_t bytes = 0;
    std::wstring sha256; // hex, empty unless requested
};

// Boot files for `mode` inside `folder`; fails with NotFound naming the missing one.
[[nodiscard]] Result<void> checkBootFiles(const std::filesystem::path& folder, BootMode mode, bool noPrompt);
// Sum of file sizes under `folder` (the ISO is this + a few MB of file system metadata).
[[nodiscard]] std::uint64_t folderSize(const std::filesystem::path& folder);

[[nodiscard]] Result<IsoResult> buildIso(const IsoOptions& options, const TaskContext& task);

// Re-exports every edition of <folder>\sources\install.(wim|esd) into a fresh file with
// `compression` and swaps it in (Lzms → install.esd, otherwise install.wim). Reclaims the space
// that servicing left behind in the WIM and sets the size of the ISO. Only for WinLove's own
// work folders (it rewrites the setup files).
[[nodiscard]] Result<void> repackInstallImage(const std::filesystem::path& folder, WimCompression compression,
                                              const TaskContext& task);

// SHA-256 of a file as lowercase hex (CNG), with progress.
[[nodiscard]] Result<std::wstring> sha256File(const std::filesystem::path& file, const TaskContext& task);

} // namespace wl::core
