#pragma once
// Component-store health of a mounted image (D-101): does the image's WinSxS have corruption, and
// can it be put right. The DISM API has DismGetImageHealth/..., but modded "lite" images on a host
// whose servicing stack moved on make the API stumble the same way updates do (DismExe.h note), so
// this runs Windows' own dism.exe on the image, with our session suspended meanwhile:
//   dism.exe /Image:<mount> /Cleanup-Image /CheckHealth            (fast: reads the stored flag)
//   dism.exe /Image:<mount> /Cleanup-Image /ScanHealth             (thorough: scans and sets it)
//   dism.exe /Image:<mount> /Cleanup-Image /RestoreHealth [/Source:... /LimitAccess]
// CheckHealth and ScanHealth are read-only; RestoreHealth changes the image (not committed here).
// RestoreHealth without a source reaches for WinSxS and Windows Update; an offline repair wants a
// `source` (another matching image's install.wim/ESD or a mounted \Windows) and LimitAccess.
#include "core/image/dism/Dism.h"

#include <string>
#include <string_view>

namespace wl::core {

enum class ImageHealthState {
    Unknown,       // DISM said nothing we recognised
    Healthy,       // no component store corruption detected
    Repairable,    // corruption found, RestoreHealth can mend it
    NonRepairable, // corruption found that cannot be repaired
};

struct ImageHealthReport {
    ImageHealthState state = ImageHealthState::Unknown;
    bool scanned = false;    // a ScanHealth (not just the cached CheckHealth flag)
    bool repaired = false;   // a RestoreHealth that put it right this run
    std::wstring detail;     // the DISM line we read the verdict from

    [[nodiscard]] bool operator==(const ImageHealthReport&) const = default;
};

// CheckHealth (scan=false, fast) or ScanHealth (scan=true, slow, reports progress) on the image.
[[nodiscard]] Result<ImageHealthReport> checkImageHealth(DismSession& session, bool scan, const TaskContext& task);

// RestoreHealth on the image. `source` is a DISM /Source: spec (e.g. "WIM:D:\install.wim:1",
// "ESD:...:1" or a path to a mounted \Windows), empty for DISM's default (WinSxS + Windows Update).
// `limitAccess` adds /LimitAccess (stay offline: use `source` only, never Windows Update).
[[nodiscard]] Result<ImageHealthReport> restoreImageHealth(DismSession& session, std::wstring_view source,
                                                           bool limitAccess, const TaskContext& task);

// The verdict a piece of /English dism.exe health output carries (for tests and reuse).
[[nodiscard]] ImageHealthState imageHealthFromOutput(std::string_view output);

} // namespace wl::core
