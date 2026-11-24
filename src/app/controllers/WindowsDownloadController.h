#pragma once
// D-093: "Windows indir" — any Windows build from Microsoft's update servers as a setup ISO.
// The builds, languages and editions come from the UUP dump API; the files from Microsoft's CDN
// (SHA-256 checked); WinLove's own converter (core/uup/UupConvert) makes the media and the ISO.
// Lists and downloads run on a network thread of their own (a 9 GB download must not hold up
// DISM); the conversion runs on the engine thread (it mounts). One job at a time;
// AppState::windowsDownload() tells the page.
#include "app/state/AppState.h"
#include "core/esd/EsdCatalog.h"
#include "core/uup/UupApps.h"
#include "core/uup/UupCatalog.h"
#include "core/uup/UupConvert.h"

#include <functional>
#include <memory>
#include <optional>

namespace wl::app {

class WindowsDownloadController {
public:
    struct Request {
        core::uup::Build build;
        std::wstring language;              // "tr-tr"
        std::vector<std::wstring> editions; // "PROFESSIONAL"
        bool updates = true;                // integrate the set's updates (longer)
        bool edge = true;
        bool esd = false;                   // install.esd: a smaller ISO
        bool apps = true;                   // the Store apps Microsoft's media has
        std::vector<std::wstring> excludedApps; // …but these
        bool netFx3 = false;                // .NET Framework 3.5
        bool resetBase = false;             // component store cleanup with /ResetBase
        std::filesystem::path output;       // the .iso
        // D-149: one source per download. Esd: Microsoft's official ESD(s) of the catalog (`esdFiles`),
        // then the same media / ISO steps; updates, Edge and the app choice are the UUP set's only.
        core::esd::Source source = core::esd::Source::Uup;
        std::vector<core::esd::Entry> esdFiles;
    };

    enum class Failed : std::uint8_t { List, Download, Convert }; // what did not work out

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(std::vector<core::uup::Build>)> builds;
        std::function<void(std::wstring id, std::vector<core::uup::Language>)> languages;
        std::function<void(std::wstring id, std::wstring language, std::vector<core::uup::Edition>)> editions;
        // The files (without links) of the selection: the sizes the page shows.
        std::function<void(std::wstring id, std::wstring language, std::vector<std::wstring> editions, core::uup::FileSet)> files;
        // The Store apps a selection gets (no frameworks): what the picker lists.
        std::function<void(std::wstring id, std::wstring language, std::vector<std::wstring> editions,
                           std::vector<core::uup::AppFeature>)> apps;
        std::function<void(const Error&, Failed what)> failed; // a list, or the job's download / conversion
        std::function<void(const core::uup::ConvertResult&)> finished;
        std::function<void()> stopped; // cancelled by the user
        // D-149: Microsoft's official ESD catalog of a product arrived (or why not).
        std::function<void(core::esd::Product, Result<core::esd::Catalog>)> esdCatalog;
    };

    WindowsDownloadController(AppState& state, Events events);
    ~WindowsDownloadController();

    void listBuilds(std::wstring search = {});
    void listLanguages(std::wstring id);
    void listEditions(std::wstring id, std::wstring language);
    void listFiles(std::wstring id, std::wstring language, std::vector<std::wstring> editions);
    void listApps(core::uup::Build build, std::wstring language, std::vector<std::wstring> editions);
    // D-149: both official catalogs (Windows 11 and 10), once per session: a second call asks only
    // for the ones that have not come (a failed one is asked again); esdCatalog event when one lands.
    void listEsdCatalogs();
    // What came already (a page made later reads it here: an event during its construction would
    // find no page to land on).
    [[nodiscard]] const core::esd::Catalog* esdCatalog(core::esd::Product product) const {
        const auto& c = m_esdCatalogs[static_cast<std::size_t>(product)];
        return c ? &*c : nullptr;
    }

    [[nodiscard]] bool running() const { return m_state.windowsDownload().has_value(); }
    void start(Request request);
    void cancel();
    // At startup (elevated): the mount a conversion left when the app closed or crashed mid-way
    // goes (core::uup::releaseWorkMount), not only once the next conversion starts.
    void releaseLeftovers();

    // Where a build's files are kept (a second run continues there): <work>\uup\<build>_<arch>_<lang>.
    [[nodiscard]] std::filesystem::path setFolder(const Request& request) const;
    // The files a request downloads: the set without Store apps; without .msu / KB packages and
    // Edge when no updates go in.
    [[nodiscard]] static std::vector<core::uup::File> filesFor(const core::uup::FileSet& set, bool updates);
    // "Win11_26H2_26300.9550_tr-tr_x64.iso"
    [[nodiscard]] static std::wstring isoName(const core::uup::Build& build, std::wstring_view language);
    // D-149: the same for an official ESD: its own build ("Win11_26H2_26300.9711_tr-tr_x64.iso").
    [[nodiscard]] static std::wstring isoName(const core::uup::Build& build, std::wstring_view language,
                                              std::wstring_view esdBuild);
    // Where the official ESDs go (a second run continues there): <work>\esd.
    [[nodiscard]] std::filesystem::path esdFolder() const;

private:
    void convert(Request request, std::filesystem::path folder);
    void startEsd(Request request);
    void convertEsd(Request request, std::vector<std::filesystem::path> files);
    void finish();

    AppState& m_state;
    Events m_events;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    // Audit CTL-14: the last request of each list. The page shows one build, one language and one
    // set of editions and drops every other answer, so a newer request makes the one before it
    // useless: it is cancelled (a waiting one then ends at once) instead of run in turn — ten rows
    // walked with the arrow keys were some forty requests, one after the other. The files have
    // two lists that live side by side: the selection's and the Store apps' ("neutral").
    std::optional<core::CancelToken> m_buildsRequest;
    std::optional<core::CancelToken> m_languagesRequest;
    std::optional<core::CancelToken> m_editionsRequest;
    std::optional<core::CancelToken> m_filesRequest;
    std::optional<core::CancelToken> m_appFilesRequest;
    std::optional<core::CancelToken> m_appsRequest;
    std::unique_ptr<core::TaskRunner> m_lists; // the API's answers (short)
    // 2026-11-11 (the owner: "Seç…" came 10-15 s after a build was picked): the Store apps' list has a
    // turn of its own, not behind the editions' and files' answers on m_lists.
    std::unique_ptr<core::TaskRunner> m_appLists;
    // D-149: the official catalogs, kept for the session (index: Product).
    std::optional<core::esd::Catalog> m_esdCatalogs[2];
    bool m_esdAsked[2] = {false, false};
    std::unique_ptr<core::TaskRunner> m_catalogs; // the official catalogs (two small requests)
    std::unique_ptr<core::TaskRunner> m_net;   // last: joined before the rest goes
};

} // namespace wl::app
