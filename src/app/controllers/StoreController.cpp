#include "app/controllers/StoreController.h"

#include "base/Log.h"

#include <atomic>
#include <format>

namespace wl::app {

StoreController::Backend StoreController::storeBackend() {
    return Backend{
        [](const std::wstring& query, const core::CancelToken& cancel) { return core::searchStore(query, cancel); },
        [](const std::wstring& id, const core::CancelToken& cancel) { return core::storeProduct(id, cancel); },
        [](const core::StoreProduct& product, const core::CancelToken& cancel) { return core::storePackages(product, cancel); },
        [](const std::vector<core::StorePackage>& packages, const std::filesystem::path& folder, const core::TaskContext& task) {
            return core::downloadStorePackages(packages, folder, task);
        },
    };
}

StoreController::StoreController(AppState& state, Events events, Backend backend)
    : m_state(state), m_events(std::move(events)), m_backend(std::move(backend)), m_net(std::make_unique<core::TaskRunner>()) {}

StoreController::~StoreController() {
    *m_alive = false;
    if (const auto& fetch = m_state.storeFetch()) {
        fetch->cancel.cancel();
    }
    m_net.reset();
}

std::wstring StoreController::architecture() const {
    const auto& mounted = m_state.mounted();
    const auto& source = m_state.source();
    if (mounted && source) {
        for (const auto& image : source->install.images) {
            if (image.index == mounted->index) {
                return core::architectureName(image.architecture);
            }
        }
    }
    return L"x64";
}

void StoreController::search(std::wstring query) {
    if (busy() || query.empty()) {
        return;
    }
    AppState::StoreFetch fetch;
    fetch.stage = AppState::StoreFetch::Stage::Searching;
    fetch.title = query;
    const core::CancelToken cancel = fetch.cancel;
    m_state.setStoreFetch(std::move(fetch));
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_net->run<std::vector<core::StoreSearchResult>>(
        [search = m_backend.search, query, cancel](const core::TaskContext&) { return search(query, cancel); },
        [this, post, alive, query](Result<std::vector<core::StoreSearchResult>> result) {
            post([this, alive, query, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_state.setStoreFetch(std::nullopt);
                if (!result) {
                    if (result.error().code != ErrorCode::Cancelled && m_events.failed) {
                        m_events.failed(result.error());
                    }
                    return;
                }
                if (m_events.results) {
                    m_events.results(query, std::move(*result));
                }
            });
        });
}

void StoreController::install(core::StoreSearchResult app) {
    if (busy()) {
        return;
    }
    AppState::StoreFetch fetch;
    fetch.stage = AppState::StoreFetch::Stage::Resolving;
    fetch.title = app.name;
    const core::CancelToken cancel = fetch.cancel;
    m_state.setStoreFetch(std::move(fetch));
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    auto last = std::make_shared<std::atomic<int>>(-1);
    auto progress = [this, post, alive, last](AppState::StoreFetch::Stage stage, std::uint64_t done, std::uint64_t total) {
        const int key = static_cast<int>(stage) * 1000 + (total ? static_cast<int>(done * 999 / total) : 0);
        if (last->exchange(key) == key) {
            return;
        }
        post([this, alive, stage, done, total] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            if (auto& f = m_state.storeFetchMutable()) {
                f->stage = stage;
                f->doneBytes = done;
                f->totalBytes = total;
                m_state.notifyStoreFetch();
            }
        });
    };
    const std::wstring arch = architecture();
    const std::filesystem::path root = m_state.settings().workRoot / L"store";
    struct Done {
        std::wstring title;
        std::filesystem::path app;
    };
    m_net->run<Done>(
        [backend = m_backend, app, arch, root, cancel, progress](const core::TaskContext&) -> Result<Done> {
            auto product = backend.product(app.productId, cancel);
            if (!product) {
                return std::unexpected(product.error());
            }
            auto all = backend.packages(*product, cancel);
            if (!all) {
                return std::unexpected(all.error());
            }
            const auto picked = core::pickStorePackages(*all, product->packageFamilyName, arch);
            if (picked.empty()) {
                const bool encrypted = std::ranges::any_of(*all, [](const core::StorePackage& p) { return p.extension.starts_with(L".e"); });
                return fail(ErrorCode::Unsupported,
                            encrypted ? std::wstring(L"the app comes only as an encrypted package (Store licence): it cannot be added to an image")
                                      : std::format(L"Windows Update has no package of this app for {}", arch),
                            product->title);
            }
            std::uint64_t total = 0;
            for (const auto& p : picked) {
                total += p.size;
            }
            const core::TaskContext task{cancel, [&](double fraction, std::wstring_view) {
                                             if (fraction >= 0) {
                                                 progress(AppState::StoreFetch::Stage::Downloading,
                                                          static_cast<std::uint64_t>(fraction * static_cast<double>(total)), total);
                                             }
                                         }};
            auto files = backend.download(picked, root / app.productId, task);
            if (!files) {
                return std::unexpected(files.error());
            }
            return Done{product->title.empty() ? app.name : product->title, files->front()};
        },
        [this, post, alive](Result<Done> result) {
            post([this, alive, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_state.setStoreFetch(std::nullopt);
                if (!result) {
                    if (result.error().code == ErrorCode::Cancelled) {
                        if (m_events.stopped) {
                            m_events.stopped();
                        }
                        return;
                    }
                    log::error("store", describe(result.error()));
                    if (m_events.failed) {
                        m_events.failed(result.error());
                    }
                    return;
                }
                if (m_events.downloaded) {
                    m_events.downloaded(std::move(result->title), std::move(result->app));
                }
            });
        });
}

void StoreController::cancel() {
    if (const auto& fetch = m_state.storeFetch()) {
        fetch->cancel.cancel();
    }
}

} // namespace wl::app
