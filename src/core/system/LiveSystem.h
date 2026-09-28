#pragma once
// The running Windows installation ("Bu bilgisayar (canlı)" card on the Source page).
#include "core/image/ImageInfo.h"

#include <cstdint>
#include <string>

namespace wl::core {

struct LiveSystemInfo {
    std::wstring productName;     // "Windows 11 Pro" (registry says "Windows 10 …" on 11; corrected)
    std::wstring displayVersion;  // "25H2"
    int build = 0;
    int ubr = 0;                  // update build revision: 26200.**6584**
    Architecture architecture = Architecture::Unknown;
    std::wstring systemDrive;     // "C:\"
    std::uint64_t driveFree = 0;
    std::uint64_t driveTotal = 0;
};

[[nodiscard]] LiveSystemInfo readLiveSystem();

} // namespace wl::core
