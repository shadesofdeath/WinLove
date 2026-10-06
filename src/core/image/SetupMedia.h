#pragma once
// AIO (D-077): the setup media of an install image that mixes Windows 10 and 11. Measured in
// VMware: the Windows 11 24H2+ media (new or previous Setup) cannot install Windows 10; the
// Windows 10 media installs both. These read what the media can install and swap the media files
// of a setup folder for another Windows' — the install image and the user's additions stay.
#include "core/image/Source.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <vector>

namespace wl::core {

// The build of the image Setup boots from (sources\boot.wim: its boot index, else the last
// image); 0 when the source has no setup media (a WIM / ESD opened on its own).
[[nodiscard]] int setupMediaBuild(const SourceInfo& source);
// The editions the source's own setup media cannot install: Windows 10 (build < 22000) when the
// media is 24H2 or newer (26100+). Empty when the media is older or unknown.
[[nodiscard]] std::vector<int> editionsMediaCannotInstall(const SourceInfo& source);

// Replaces the setup files of `setupFolder` (boot.wim, setup.exe, efi\, boot\, sources\…) with
// those of `from`: a Windows 10 ISO, a setup folder, or a Media Creation Tool ESD (its "Windows
// Setup Media" image is applied, its two Windows PE images become boot.wim). What stays:
// sources\install.wim / .esd / .swm, sources\$OEM$ and autounattend.xml. Refused when the new
// media is 24H2+ itself or of another architecture than the install image. The new files are
// complete in a staging folder next to `setupFolder` before anything there is deleted.
[[nodiscard]] Result<void> replaceSetupMedia(const std::filesystem::path& setupFolder, const std::filesystem::path& from,
                                             const TaskContext& task);

} // namespace wl::core
