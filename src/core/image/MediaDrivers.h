#pragma once
// D-134: the third-party drivers a Windows image already carries, read from its file list — no
// mount, no admin, an ISO read in place. Windows\INF\oemN.inf is the copy PnP / DISM keeps of each
// added package; the driver store folder holding a file with the same content (the same SHA-1 in
// the file list) gives its original name ("pvscsi.inf_amd64_<hash>\pvscsi.inf").
// What the Drivers page's boot tab shows as "already in boot.wim", and how the ISO page tells a
// storage driver that went only into Setup's image from one the installed Windows has too.
#include "core/image/DriverInf.h"
#include "core/image/ImageInfo.h"
#include "core/image/Source.h"
#include "core/image/wim/WimVerify.h"

#include <string>
#include <vector>

namespace wl::core {

struct WimDriver {
    std::wstring publishedName; // "oem0.inf"
    std::wstring originalName;  // "pvscsi.inf"; empty when no driver store file has its content
    DriverInf inf;              // read from its content (path = the name below)
    [[nodiscard]] const std::wstring& name() const noexcept { return originalName.empty() ? publishedName : originalName; }
};

// The third-party packages of one opened edition, by published name.
[[nodiscard]] std::vector<WimDriver> thirdPartyDrivers(const WimEdition& edition);

struct MediaDrivers {
    int bootIndex = 0;                                  // Setup's image in boot.wim; 0: no boot.wim
    Architecture architecture = Architecture::Unknown;  // of the setup media (boot.wim, else the install image)
    std::vector<WimDriver> boot;                        // in boot.wim's Setup image
    std::vector<WimDriver> install;                     // in any edition of the install image, each package once
    bool installRead = false;                           // false: an ESD / unreadable install image (nothing known)
};

// Both lists of a setup medium (ISO or setup folder); for a bare WIM only `install`. Reads the boot
// image's Setup index and every edition of the install image (~0.2 s each, LZX file lists).
[[nodiscard]] Result<MediaDrivers> readMediaDrivers(const SourceInfo& source);

// Whether `inf` (a scanned package) is one of `in`: the same INF name and DriverVer version (a
// newer version of a driver the image has is not "already there").
[[nodiscard]] bool containsDriver(const std::vector<WimDriver>& in, const DriverInf& inf);

} // namespace wl::core
