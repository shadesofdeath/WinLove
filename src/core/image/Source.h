#pragma once
// Opening what the user picked (docs/ARCHITECTURE.md §2.2): an ISO, or a WIM/ESD/SWM directly.
// For an ISO the install image is found inside it (sources/install.wim|esd|swm) and read in place.
#include "core/image/ImageFormat.h"
#include "core/image/UdfImage.h"
#include "core/image/VirtualDisk.h"
#include "core/image/WimFile.h"

#include <filesystem>
#include <optional>

namespace wl::core {

struct SourceInfo {
    std::filesystem::path path;
    ImageFormat format = ImageFormat::Unknown;
    std::wstring volumeLabel;              // ISO only
    std::wstring installImage;             // path inside the ISO ("sources/install.esd"), or the file itself
    std::uint64_t installImageSize = 0;
    WimFile install;
    std::optional<WimFile> boot;           // sources/boot.wim when present (ISO only)
    // D-141: a VHD / VHDX — `install` then holds its one installed Windows (index 1, from the
    // image's registry; no WIM behind it), `installImage` the file's name.
    std::optional<VirtualDiskInfo> disk;
};

[[nodiscard]] Result<SourceInfo> openSource(const std::filesystem::path& path);

// 2026-11-11: the source to open for an image DISM reports as mounted (WinLove's own from an earlier
// run, another tool's, dism.exe's). The setup folder only when the image really is the install image
// of setup media — <folder>\sources\install.wim|esd|swm (any case) with <folder>\setup.exe or
// sources\boot.wim next to it —, else the image file itself, wherever it lies. Without the "\\?\"
// prefix DISM may answer with.
[[nodiscard]] std::filesystem::path sourceForMountedImage(const std::filesystem::path& imagePath);
// openSource(sourceForMountedImage(imagePath)); when the setup folder cannot be read, the image
// file itself (the edition list is the WIM's either way).
[[nodiscard]] Result<SourceInfo> openMountedImageSource(const std::filesystem::path& imagePath);

// The install image of an open source as bytes: the WIM itself, the one under sources\ of a setup
// folder, or — read in place — the one inside the ISO.
[[nodiscard]] Result<std::shared_ptr<const ByteSource>> openInstallImage(const SourceInfo& source);
// D-134: the setup media's sources\boot.wim as bytes (read in place inside an ISO). NotFound for a
// source without one (a bare WIM / ESD).
[[nodiscard]] Result<std::shared_ptr<const ByteSource>> openBootImage(const SourceInfo& source);
// The install image as a file wimgapi can open: the WIM / ESD / SWM itself, the one in a setup
// folder, or — for an ISO — extracted into `scratch` (`extracted` says so: the caller deletes it).
[[nodiscard]] Result<std::filesystem::path> installImageFile(const SourceInfo& source, const std::filesystem::path& scratch,
                                                             bool& extracted, const TaskContext& task);

} // namespace wl::core
