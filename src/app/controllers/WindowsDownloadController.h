#pragma once
// D-093: "Windows indir" — any Windows build from Microsoft's update servers as a setup ISO.
// The builds, languages and editions come from the UUP dump API; the files from Microsoft's CDN
// (SHA-256 checked); WinLove's own converter (core/uup/UupConvert) makes the media and the ISO.
// Lists and downloads run on a network thread of their own (a 9 GB download must not hold up
// DISM); the conversion runs on the engine thread (it mounts). One job at a time;
// AppState::windowsDownload() tells the page.
#include "app/state/AppState.h"
#include "core/uup/UupCatalog.h"
#include "core/uup/UupConvert.h"

#include <functional>
#include <memory>

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
        std::filesystem::path output;       // the .iso
    };

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(std::vector<core::uup::Build>)> builds;
        std::function<void(std::wstring id, std::vector<core::uup::Language>)> languages;
        std::function<void(std::wstring id, std::wstring language, std::vector<core::uup::Edition>)> editions;
        // The files (without links) of the selection: the sizes the page shows.
        std::function<void(std::wstring id, std::wstring language, std::vector<std::wstring> editions, core::uup::FileSet)> files;
        std::function<void(const Error&, bool job)> failed; // a list (false) or the job (true)
        std::function<void(const core::uup::ConvertResult&)> finished;
        std::function<void()> stopped; // cancelled by the user
    };

    WindowsDownloadController(AppState& state, Events events);
    ~WindowsDownloadController();

    void listBuilds(std::wstring search = {});
    void listLanguages(std::wstring id);
    void listEditions(std::wstring id, std::wstring language);
    void listFiles(std::wstring id, std::wstring language, std::vector<std::wstring> editions);

    [[nodiscard]] bool running() const { return m_state.windowsDownload().has_value(); }
    void start(Request request);
    void cancel();

    // Where a build's files are kept (a second run continues there): <work>\uup\<build>_<arch>_<lang>.
    [[nodiscard]] std::filesystem::path setFolder(const Request& request) const;
    // The files a request downloads: the set without Store apps; without .msu / KB packages and
    // Edge when no updates go in.
    [[nodiscard]] static std::vector<core::uup::File> filesFor(const core::uup::FileSet& set, bool updates);
    // "Win11_26H2_26300.9550_tr-tr_x64.iso"
    [[nodiscard]] static std::wstring isoName(const core::uup::Build& build, std::wstring_view language);

private:
    void convert(Request request, std::filesystem::path folder);
    void finish();

    AppState& m_state;
    Events m_events;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_lists; // the API's answers (short)
    std::unique_ptr<core::TaskRunner> m_net;   // last: joined before the rest goes
};

} // namespace wl::app
