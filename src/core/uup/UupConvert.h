#pragma once
// D-093: a downloaded UUP set (UupDownload) → Windows setup media → ISO, with Windows' own tools
// only (wimgapi, DISM, IMAPI). What uup-converter-wimlib does with wimlib, step by step:
//   1. references: an edition's metadata ESD (professional_tr-tr.esd) lists the files of its
//      install image; they are in the package ESDs next to it and in the feature-on-demand .cab
//      files (expanded, captured as reference WIMs — the ones with an update.mum);
//   2. the setup media: index 1 of the metadata ESD ("Windows Setup Media") applied to a folder;
//   3. install.wim: index 3 of each edition's metadata ESD exported with those references;
//   4. WinRE: index 2, exported on its own; it goes into every edition as
//      Windows\System32\Recovery\Winre.wim (the image is mounted for it);
//   5. updates (optional): Edge (dism /Add-Edge), servicing stack, enablement / .NET / other
//      packages, then the cumulative update (its checkpoint next to it) into every edition; the
//      Setup dynamic update over the media's sources\;
//   6. boot.wim from WinRE: "Microsoft Windows PE" (9) and "Microsoft Windows Setup" (2, boots);
//   7. the ISO (IsoBuilder), label CPRA_X64FRE_TR-TR_DV9 style.
// Needs an elevated process (reference capture, mounts). Long: minutes without updates, up to an
// hour or more with them.
#include "core/image/WimFile.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {
class Dism;
}

namespace wl::core::uup {

enum class UpdateRole : std::uint8_t { ServicingStack, Cumulative, Checkpoint, Enablement, DotNet, SafeOs, SetupDu, Other };

struct UupEdition {
    std::filesystem::path metadata; // professional_tr-tr.esd
    std::wstring name;              // "Windows 11 Pro"
    std::wstring description;
    std::wstring editionId;         // "Professional"
    std::wstring language;          // "tr-TR"
    std::wstring architecture;      // "x64" | "arm64" | "x86"
    int build = 0;
    int revision = 0;
};

struct UupSetFiles {
    std::vector<UupEdition> editions;
    std::vector<std::filesystem::path> packageEsds;  // references as they are
    std::vector<std::filesystem::path> featureCabs;  // references once expanded and captured
    std::vector<std::filesystem::path> updates;      // Windows1x.0-KB*.msu / .cab
    std::filesystem::path edge;                      // Edge.wim, empty when none
    std::filesystem::path aggregatedMetadata;        // *.AggregatedMetadata.cab: the app database
    std::filesystem::path apps;                      // <folder>\apps when the Store apps were downloaded
};

// What is in `folder` (a downloaded set). NotFound when there is no metadata ESD.
[[nodiscard]] Result<UupSetFiles> scanUupFolder(const std::filesystem::path& folder);

// An update package's role from what it holds (expand.exe: its update.mum and file list) — the
// names of UUP's update files do not say. Checkpoint: one of the known baseline LCUs.
[[nodiscard]] UpdateRole updateRole(const std::filesystem::path& file);
// Pure part (unit-tested): from the file name, the update.mum text (lower-cased or not) and the
// list of files in the package; `hasMum` false = the package has no update.mum.
[[nodiscard]] UpdateRole updateRoleFrom(std::wstring_view fileName, bool hasMum, std::wstring_view mum,
                                        std::wstring_view fileList);

struct ConvertOptions {
    std::filesystem::path uupFolder;   // the downloaded set
    std::filesystem::path workFolder;  // scratch (references, WinRE, mount folder); removed at the end
    std::filesystem::path mediaFolder; // the setup media being built; removed at the end when an ISO is made
    std::filesystem::path output;      // .iso; empty = keep the media folder only
    bool updates = true;               // integrate the set's updates (DISM; long)
    bool edge = true;                  // Edge.wim into the editions (with updates only: the image is mounted anyway)
    bool apps = true;                  // the Store apps (UupApps) when the set has them in <uupFolder>\apps
    std::vector<std::wstring> excludedApps; // app ids left out (the picker); required ones stay
    bool netFx3 = false;               // .NET Framework 3.5 from the media's sources\sxs
    bool resetBase = false;            // component cleanup with /ResetBase (smaller; updates cannot be removed)
    WimCompression compression = WimCompression::Lzx; // Lzms → sources\install.esd
};

struct ConvertResult {
    std::filesystem::path iso;    // empty when no ISO was asked for
    std::filesystem::path media;  // the media folder when it is kept
    std::wstring label;           // the volume label
    std::wstring version;         // the editions' version after the updates ("10.0.26300.9550")
    std::vector<std::wstring> editions;
    std::vector<std::wstring> warnings; // a package that would not go in, …
    std::uint64_t bytes = 0;
};

// The ISO volume label of a set: CPRA_X64FRE_TR-TR_DV9 (one Pro edition), CCSA_… (several).
[[nodiscard]] std::wstring isoLabel(const UupSetFiles& set);

[[nodiscard]] Result<ConvertResult> convertUup(Dism& dism, const ConvertOptions& options, const TaskContext& task);

} // namespace wl::core::uup
