#pragma once
// D-066: "Mağazadan ekle…" on the Uygulamalar page — search the Microsoft Store, pick an app, its
// packages and frameworks for the mounted image's architecture are downloaded from Windows Update
// into <work>\store\<product id> (SHA-256 checked) and the app is queued like a dropped package
// (AppsController finds the frameworks next to it). Network work on a thread of its own; one at a
// time; AppState::storeFetch() tells the page.
#include "app/state/AppState.h"
#include "core/store/MsStore.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace wl::app {

class StoreController {
public:
    struct Backend {
        std::function<Result<std::vector<core::StoreSearchResult>>(const std::wstring&, const core::CancelToken&)> search;
        std::function<Result<core::StoreProduct>(const std::wstring&, const core::CancelToken&)> product;
        std::function<Result<std::vector<core::StorePackage>>(const core::StoreProduct&, const core::CancelToken&)> packages;
        std::function<Result<std::vector<std::filesystem::path>>(const std::vector<core::StorePackage>&, const std::filesystem::path&,
                                                                 const core::TaskContext&)>
            download;
    };
    [[nodiscard]] static Backend storeBackend();

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(const std::wstring& query, std::vector<core::StoreSearchResult>)> results;
        std::function<void(const Error&)> failed;
        std::function<void(std::wstring title, std::filesystem::path app)> downloaded; // the app's own file
        std::function<void()> stopped;
    };

    StoreController(AppState& state, Events events, Backend backend = storeBackend());
    ~StoreController();

    [[nodiscard]] bool busy() const { return m_state.storeFetch().has_value(); }
    void search(std::wstring query);
    void install(core::StoreSearchResult app);
    void cancel();
    void drain() { m_net->drain(); }

    [[nodiscard]] std::wstring architecture() const; // the mounted image's: x64 | arm64 | x86

private:
    AppState& m_state;
    Events m_events;
    Backend m_backend;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_net;
};

} // namespace wl::app
