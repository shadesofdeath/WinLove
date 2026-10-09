#pragma once
// D-093: the Store apps Microsoft's own media provisions (Store, Calculator, Photos, Terminal …),
// from a UUP set. They are not in the edition's files: the set's AggregatedMetadata cabinet has an
// app database (DesktopTargetCompDB_App_Neutral) — every app with its frameworks, its licence and
// its packages (a small "flat" bundle, the main package per architecture, the language / scale
// resource packages), each with its SHA-256 — and the files themselves are UUP dump's
// language "neutral", edition "app" set, matched by that hash. On disk each app gets a folder:
// <apps>\<app id>\ (the bundle next to its packages, under the names the bundle expects, and
// License.xml); the frameworks go to <apps>\Frameworks\. DISM provisions them (AppxInstall).
// Which apps an edition gets: all of them, but the media apps and codecs not on N editions and
// the Surface Hub apps on Team only — the lists Microsoft's media follows.
#include "base/Result.h"
#include "core/tasks/Task.h"
#include "core/uup/UupCatalog.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {
class DismSession;
}

namespace wl::core::uup {

struct AppPackage {
    std::wstring id;       // "Microsoft.WindowsStore_22401.1400.6.0_x64__8wekyb3d8bbwe"
    std::wstring fileName; // what the bundle expects: "StorePackage_22401.1400.6.0_x64.msix"
    std::wstring sha256;   // lower-case hex
    std::uint64_t size = 0;
    bool bundle = false;   // the app's bundle (the one DISM is given)
};

struct AppFeature {
    std::wstring id; // "Microsoft.WindowsStore_8wekyb3d8bbwe"
    bool framework = false;
    std::vector<std::wstring> dependencies; // framework ids
    std::string license;                    // the licence XML; empty = none (frameworks)
    std::vector<AppPackage> packages;
};

// ---- pure (unit-tested) ----
[[nodiscard]] Result<std::vector<AppFeature>> parseAppCompDb(std::string_view xml);

// How the picker groups the apps (the converter's lists: client apps, media apps, codecs).
enum class AppGroup : std::uint8_t { Essential, Media, Codecs, Other };
[[nodiscard]] AppGroup appGroup(std::wstring_view appId);
// Apps a Windows must keep (they cannot be left out): Windows Security's app, and App Installer
// (winget: WinLove's Programs install through it at the first sign-in).
[[nodiscard]] bool appRequired(std::wstring_view appId);
// "Microsoft.WindowsCalculator_8wekyb3d8bbwe" → "windowscalculator": the short key the app's
// display name has in the strings (uupApps.<key>).
[[nodiscard]] std::wstring appKey(std::wstring_view appId);
// Leaves out the apps in `excluded` (ids, any case); frameworks and required apps stay.
void excludeApps(std::vector<AppFeature>& plan, const std::vector<std::wstring>& excluded);
// Does an edition ("Professional", "CoreN", "PPIPro") get this app?
[[nodiscard]] bool appForEdition(std::wstring_view appId, std::wstring_view editionId);
// The packages an image of `architecture` ("amd64" / "arm64" / "x86") needs: neutral ones (the
// bundle, resources) and its own architecture; x86 too on amd64, x64 too on arm64 (frameworks).
[[nodiscard]] bool packageForArchitecture(std::wstring_view packageId, std::wstring_view architecture);
// The apps for `editions` and the frameworks they need, the packages trimmed to the architecture.
[[nodiscard]] std::vector<AppFeature> appsFor(const std::vector<AppFeature>& all, const std::vector<std::wstring>& editions,
                                              std::wstring_view architecture);
// The files to download: each needed package matched by its SHA-256 among `available` (the app
// set), named "<folder>\<file name>" relative to the apps folder. `missing` gets the packages the
// set has no file for.
[[nodiscard]] std::vector<File> appFiles(const std::vector<AppFeature>& apps, const std::vector<File>& available,
                                         std::vector<std::wstring>* missing = nullptr);
[[nodiscard]] std::wstring appFolderName(const AppFeature& app); // "Frameworks" or the app id

// ---- files ----
// The app database out of a set's *.AggregatedMetadata.cab (expand.exe, twice: it is a cab in a cab).
[[nodiscard]] Result<std::vector<AppFeature>> readAppCompDb(const std::filesystem::path& aggregatedMetadataCab,
                                                            const std::filesystem::path& scratch);
// Writes <apps>\<app>\License.xml for every app with a licence.
[[nodiscard]] Result<void> writeLicenses(const std::vector<AppFeature>& apps, const std::filesystem::path& appsFolder);

// What downloading the apps takes: the plan, and its files named "apps\<folder>\<file>" (relative
// to the set folder, with links); the licences are written into <setFolder>\apps. Needs the set's
// AggregatedMetadata.cab in `setFolder` already.
struct AppDownload {
    std::vector<AppFeature> plan;
    std::vector<File> files;
    std::vector<std::wstring> missing; // packages the app set has no file for (left out)
};
[[nodiscard]] Result<AppDownload> planAppDownload(std::wstring_view id, const std::filesystem::path& setFolder,
                                                  const std::vector<std::wstring>& editions, std::wstring_view architecture,
                                                  const CancelToken& cancel,
                                                  const std::vector<std::wstring>& excluded = {});
// The apps a selection gets (no frameworks), for the picker: the set's AggregatedMetadata.cab is
// downloaded into `setFolder` (it stays for the download) and its app database read.
[[nodiscard]] Result<std::vector<AppFeature>> setApps(std::wstring_view id, std::wstring_view language,
                                                      const std::vector<std::wstring>& editions,
                                                      std::wstring_view architecture,
                                                      const std::filesystem::path& setFolder, const CancelToken& cancel);

// The set's *.AggregatedMetadata.cab in `folder`; empty when there is none.
[[nodiscard]] std::filesystem::path aggregatedMetadata(const std::filesystem::path& folder);
// Downloads the apps `editions` get on `architecture` into <setFolder>\apps, licences included (the
// set's app database says which; UUP dump's app set of build `id` has the files). Returns how many
// apps (frameworks not counted).
[[nodiscard]] Result<std::size_t> downloadApps(std::wstring_view id, const std::filesystem::path& setFolder,
                                               const std::vector<std::wstring>& editions, std::wstring_view architecture,
                                               const TaskContext& task);
// Provisions `apps` from `appsFolder` into the mounted image of `session`: frameworks, then every app
// (its bundle, or its only package) with its licence. An app whose files are not there, or that DISM
// refuses, is a warning, not a failure. Returns the warnings.
[[nodiscard]] std::vector<std::wstring> provisionApps(DismSession& session, const std::vector<AppFeature>& apps,
                                                      const std::filesystem::path& appsFolder, const TaskContext& task);

} // namespace wl::core::uup
