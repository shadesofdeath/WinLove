#pragma once
// D-061: "Dil ekle…" on the Diller page — the language files of the mounted image's own build are
// looked up (uupdump.net's index of Windows Update), the user picks languages and parts, the files
// are downloaded from Microsoft into <work>\languages\<build> (SHA-256 checked, kept for the next
// time) and queued as language packages. Network work runs on a thread of its own, one lookup /
// download at a time; AppState::languageFetch() tells the page. The list of a build is kept for
// the session: opening the dialog again does not ask uupdump.net again.
#include "app/state/AppState.h"
#include "core/updates/UupLanguages.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace wl::app {

// What the image's Windows is, for the lookup.
struct LanguageTarget {
    int build = 0, revision = 0;
    std::wstring architecture = L"x64";
    [[nodiscard]] std::wstring key() const; // "26200.8037-x64"
};

class LanguageFetchController {
public:
    // The network side, replaceable in unit tests.
    struct Backend {
        std::function<Result<std::vector<core::UupLanguage>>(const LanguageTarget&, const core::CancelToken&)> find;
        std::function<Result<std::vector<std::filesystem::path>>(const std::vector<core::UupLanguageFile>&,
                                                                 const std::filesystem::path& folder, const core::TaskContext&)>
            download;
    };
    [[nodiscard]] static Backend uupBackend();

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(const LanguageTarget&, const std::vector<core::UupLanguage>&)> offers; // lookup done
        std::function<void(const Error&, bool download)> failed;
        std::function<void(std::vector<std::filesystem::path>)> downloaded; // every file there, in install order
        std::function<void()> stopped;                                       // cancelled by the user
    };

    LanguageFetchController(AppState& state, Events events, Backend backend = uupBackend());
    ~LanguageFetchController();

    [[nodiscard]] static std::optional<LanguageTarget> targetFor(const AppState& state);
    [[nodiscard]] std::filesystem::path folder(const LanguageTarget& target) const; // <work>\languages\26200.8037-x64

    [[nodiscard]] bool busy() const { return m_state.languageFetch().has_value(); }
    // The image's languages on offer: from the session's cache, or looked up. No-op while busy.
    void find();
    void download(std::vector<core::UupLanguageFile> files);
    void cancel();
    void drain() { m_net->drain(); } // tests

private:
    AppState& m_state;
    Events m_events;
    Backend m_backend;
    std::map<std::wstring, std::vector<core::UupLanguage>> m_cache; // by LanguageTarget::key()
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_net; // last: joined before the rest goes
};

} // namespace wl::app
