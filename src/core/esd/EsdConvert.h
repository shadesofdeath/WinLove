#pragma once
// D-149: Microsoft's ESD (EsdCatalog) → Windows setup media → ISO, with the pieces the UUP path uses
// (wimgapi, DISM, IsoBuilder; the result is uup::ConvertResult). An MCT ESD is complete on its own:
//   1 "Windows Setup Media"            → the media folder (core::setupMediaFromEsd)
//   2 "Microsoft Windows PE (amd64)"   ┐ sources\boot.wim, Microsoft's own (boot index 2)
//   3 "Microsoft Windows Setup (amd64)"┘
//   4… the editions (Home, Pro, Education …, WinRE and Edge already inside) → sources\install.wim
// Optional, each edition mounted once: .NET Framework 3.5 from the media's sources\sxs, component
// cleanup with /ResetBase. No updates (the ESD carries none) and no app selection (Microsoft's
// apps are in the image already). Needs an elevated process.
#include "core/image/ImageInfo.h"
#include "core/image/WimFile.h"
#include "core/tasks/Task.h"
#include "core/uup/UupConvert.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace wl::core {
class Dism;
}

namespace wl::core::esd {

struct EsdEdition {
    int index = 0;
    std::wstring name;        // "Windows 11 Pro"
    std::wstring description;
    std::wstring editionId;   // "Professional"
    std::wstring language;    // "tr-TR"
    std::wstring architecture; // "x64"
};

// What an MCT ESD holds, by name and kind rather than position. Unsupported when it is not one
// (no "Windows Setup Media", not two Windows PE images, no edition).
struct Layout {
    int media = 0;
    std::vector<int> windowsPe; // PE, then Setup
    std::vector<EsdEdition> editions;
};
[[nodiscard]] Result<Layout> esdLayout(std::span<const ImageInfo> images);
[[nodiscard]] Result<Layout> readLayout(const std::filesystem::path& esd);

// The editions to export, in the ESDs' own order: those whose EditionID is one of `wanted`
// (case-insensitive; UUP's "PROFESSIONAL" finds "Professional"); every edition of the first ESD
// when `wanted` is empty. `missing`: the wanted ones no ESD has.
struct Selected {
    std::filesystem::path esd;
    EsdEdition edition;
};
[[nodiscard]] std::vector<Selected> selectEditions(std::span<const std::pair<std::filesystem::path, Layout>> esds,
                                                   std::span<const std::wstring> wanted, std::vector<std::wstring>* missing = nullptr);

struct ConvertOptions {
    std::vector<std::filesystem::path> esds;  // the downloaded files (the first gives the setup media)
    std::vector<std::wstring> editions;       // EditionIDs / UUP codes; empty = all of the first
    std::filesystem::path workFolder;         // scratch (mount folder); removed at the end
    std::filesystem::path mediaFolder;        // the media being built; removed once the ISO is made
    std::filesystem::path output;             // .iso; empty = keep the media folder
    bool netFx3 = false;
    bool resetBase = false;
    WimCompression compression = WimCompression::Lzx; // Lzms → sources\install.esd
};

[[nodiscard]] Result<uup::ConvertResult> convertEsd(Dism& dism, const ConvertOptions& options, const TaskContext& task);

} // namespace wl::core::esd
