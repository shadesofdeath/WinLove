#pragma once
// D-080: the recovery environment of an install image brought up to date, as Microsoft's media
// refresh does it ("Update Windows installation media with Dynamic Update", steps 1 and 6–8):
// Windows\System32\Recovery\Winre.wim is copied out, mounted, given the servicing stack of the
// latest cumulative update (its known 0x8007007E failure is ignored, as Microsoft's script does)
// and the Safe OS dynamic update, cleaned (/ResetBase /Defer), exported to drop what the commit
// left behind, and put back hidden + system. A cumulative update of the main image never reaches
// WinRE: without this it stays as old as the media.
#include "base/Result.h"
#include "core/image/dism/Dism.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace wl::core {

inline constexpr std::wstring_view kWinReRelative = L"Windows\\System32\\Recovery\\Winre.wim";

struct WinReUpdate {
    std::filesystem::path safeOs; // the Safe OS dynamic update (.cab), required
    std::filesystem::path lcu;    // the latest cumulative update (.msu), optional: its servicing stack first
};

struct WinReUpdateReport {
    std::wstring versionBefore; // "10.0.26100.1742"
    std::wstring versionAfter;
    std::uint64_t bytesBefore = 0;
    std::uint64_t bytesAfter = 0;
};

// <mountDir> is the mounted install image; <workDir> gets winre.wim, winre2.wim and the mount
// folder (removed afterwards). NotFound when the image has no WinRE (removed on Bileşenler).
// Whatever fails after the mount, WinRE is unmounted without saving and the image's file stays.
[[nodiscard]] Result<WinReUpdateReport> updateWinRe(Dism& dism, const std::filesystem::path& mountDir,
                                                    const std::filesystem::path& workDir, const WinReUpdate& update,
                                                    const TaskContext& task);

} // namespace wl::core
