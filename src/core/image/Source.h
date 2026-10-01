#pragma once
// Opening what the user picked (docs/ARCHITECTURE.md §2.2): an ISO, or a WIM/ESD/SWM directly.
// For an ISO the install image is found inside it (sources/install.wim|esd|swm) and read in place.
#include "core/image/ImageFormat.h"
#include "core/image/UdfImage.h"
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
};

[[nodiscard]] Result<SourceInfo> openSource(const std::filesystem::path& path);

// The install image of an open source as bytes: the WIM itself, the one under sources\ of a setup
// folder, or — read in place — the one inside the ISO.
[[nodiscard]] Result<std::shared_ptr<const ByteSource>> openInstallImage(const SourceInfo& source);
// The install image as a file wimgapi can open: the WIM / ESD / SWM itself, the one in a setup
// folder, or — for an ISO — extracted into `scratch` (`extracted` says so: the caller deletes it).
[[nodiscard]] Result<std::filesystem::path> installImageFile(const SourceInfo& source, const std::filesystem::path& scratch,
                                                             bool& extracted, const TaskContext& task);

} // namespace wl::core
