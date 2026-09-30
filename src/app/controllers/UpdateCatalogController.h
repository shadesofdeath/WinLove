#pragma once
// D-046: "Güncellemeleri bul" on the Updates page — the Microsoft Update Catalog is searched for
// the mounted image's Windows version, the user ticks what to take, the files are downloaded into
// <work>\updates (resumable, SHA-256 checked) and the packages are queued like dropped ones.
// Network work runs on a thread of its own: a 5 GB download must not hold up DISM (engine) or
// source reads (reader). One search / download at a time; AppState::updateFetch() tells the page.
#include "app/state/AppState.h"
#include "core/updates/UpdateCatalog.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace wl::app {

class UpdateCatalogController {
public:
    // The network side, replaceable in unit tests.
    struct Backend {
        std::function<Result<std::vector<core::CatalogOffer>>(const core::CatalogTarget&, const core::CancelToken&)> find;
        std::function<Result<core::DownloadedUpdate>(const core::CatalogEntry&, const std::filesystem::path& folder,
                                                     const core::TaskContext&)> download;
    };
    [[nodiscard]] static Backend catalogBackend();

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(const core::CatalogTarget&, std::vector<core::CatalogOffer>)> offers; // search done
        std::function<void(const Error&, bool download)> failed;                              // search / download
        std::function<void(std::vector<core::DownloadedUpdate>)> downloaded;                  // every file there
        std::function<void()> stopped;                                                        // cancelled by the user
    };

    UpdateCatalogController(AppState& state, Events events, Backend backend = catalogBackend());
    ~UpdateCatalogController();

    // What the mounted image is, in catalog terms; nullopt without a mount (the queue needs one).
    [[nodiscard]] static std::optional<core::CatalogTarget> targetFor(const AppState& state);
    [[nodiscard]] std::filesystem::path folder() const; // <work>\updates

    [[nodiscard]] bool busy() const { return m_state.updateFetch().has_value(); }
    // No-op while busy or without a target (the caller says why).
    void search();
    void download(std::vector<core::CatalogEntry> entries);
    void cancel();
    void drain() { m_net->drain(); } // tests: wait for the network thread

private:
    void finish(); // clears AppState::updateFetch

    AppState& m_state;
    Events m_events;
    Backend m_backend;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_net; // last: joined before the rest goes
};

} // namespace wl::app
