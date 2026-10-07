#include "app/controllers/UpdateCatalogController.h"

#include "base/Log.h"

#include <atomic>
#include <cmath>
#include <format>

namespace wl::app {

UpdateCatalogController::Backend UpdateCatalogController::catalogBackend() {
    return Backend{
        [](const core::CatalogTarget& target, const core::CancelToken& cancel) {
            return core::findCatalogUpdates(target, cancel);
        },
        [](const core::CatalogEntry& entry, const std::filesystem::path& folder, const core::TaskContext& task) {
            return core::downloadCatalogUpdate(entry, folder, task);
        },
    };
}

UpdateCatalogController::UpdateCatalogController(AppState& state, Events events, Backend backend)
    : m_state(state), m_events(std::move(events)), m_backend(std::move(backend)),
      m_net(std::make_unique<core::TaskRunner>()) {}

UpdateCatalogController::~UpdateCatalogController() {
    *m_alive = false;
    if (const auto& fetch = m_state.updateFetch()) {
        fetch->cancel.cancel(); // the runner joins the running job below
    }
    m_net.reset();
}

std::optional<core::CatalogTarget> UpdateCatalogController::targetFor(const AppState& state) {
    const auto& mounted = state.mounted();
    const auto& source = state.source();
    if (!mounted || !source) {
        return std::nullopt;
    }
    for (const auto& image : source->install.images) {
        if (image.index == mounted->index) {
            auto target = core::catalogTarget(image.build, image.spBuild, core::architectureName(image.architecture));
            target.dynamicUpdates = true; // D-080: WinRE (Safe OS) and the setup media (Setup) too
            return target;
        }
    }
    return std::nullopt;
}

std::filesystem::path UpdateCatalogController::folder() const {
    return m_state.settings().workRoot / L"updates";
}

void UpdateCatalogController::finish() {
    m_state.setUpdateFetch(std::nullopt);
}

void UpdateCatalogController::search() {
    const auto target = targetFor(m_state);
    if (busy() || !target) {
        return;
    }
    AppState::UpdateFetch fetch;
    const core::CancelToken cancel = fetch.cancel;
    m_state.setUpdateFetch(std::move(fetch));
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_net->run<std::vector<core::CatalogOffer>>(
        [find = m_backend.find, target = *target, cancel](const core::TaskContext&) { return find(target, cancel); },
        [this, post, alive, target = *target](Result<std::vector<core::CatalogOffer>> result) {
            post([this, alive, target, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                finish();
                if (!result) {
                    if (result.error().code == ErrorCode::Cancelled) {
                        return;
                    }
                    log::error("updates", describe(result.error()));
                    if (m_events.failed) {
                        m_events.failed(result.error(), false);
                    }
                    return;
                }
                if (m_events.offers) {
                    m_events.offers(target, std::move(*result));
                }
            });
        });
}

void UpdateCatalogController::download(std::vector<core::CatalogEntry> entries) {
    if (busy() || entries.empty()) {
        return;
    }
    AppState::UpdateFetch fetch;
    fetch.stage = AppState::UpdateFetch::Stage::Downloading;
    fetch.kb = entries.front().kb;
    for (const auto& e : entries) {
        fetch.totalBytes += e.size;
    }
    const core::CancelToken cancel = fetch.cancel;
    m_state.setUpdateFetch(std::move(fetch));
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // Progress to the UI only when the update, the stage or the per-mille changes.
    auto last = std::make_shared<std::atomic<std::int64_t>>(-1);
    auto report = [this, post, alive, last](std::size_t index, std::wstring kb, AppState::UpdateFetch::Stage stage,
                                            std::uint64_t done, std::uint64_t total) {
        const std::int64_t key = static_cast<std::int64_t>(index) * 10'000 + static_cast<std::int64_t>(stage) * 1'000 +
                                 (total ? static_cast<std::int64_t>(done * 999 / total) : 0);
        if (last->exchange(key) == key) {
            return;
        }
        post([this, alive, kb = std::move(kb), stage, done] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            if (auto& f = m_state.updateFetchMutable()) {
                f->kb = kb;
                f->stage = stage;
                f->doneBytes = done;
                m_state.notifyUpdateFetch();
            }
        });
    };
    const std::filesystem::path target = folder();
    m_net->run<std::vector<core::DownloadedUpdate>>(
        [download = m_backend.download, entries = std::move(entries), target, cancel,
         report](const core::TaskContext&) -> Result<std::vector<core::DownloadedUpdate>> {
            std::uint64_t total = 0;
            for (const auto& e : entries) {
                total += e.size;
            }
            std::vector<core::DownloadedUpdate> done;
            std::uint64_t base = 0;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                const core::TaskContext task{cancel, [&](double fraction, std::wstring_view stage) {
                                                 const bool verifying = fraction < 0 || stage == L"verify";
                                                 const auto size = static_cast<double>(entry.size);
                                                 const auto at = verifying ? base + entry.size
                                                                           : base + static_cast<std::uint64_t>(
                                                                                        std::clamp(fraction, 0.0, 1.0) * size);
                                                 report(i, entry.kb,
                                                        verifying ? AppState::UpdateFetch::Stage::Verifying
                                                                  : AppState::UpdateFetch::Stage::Downloading,
                                                        at, total);
                                             }};
                auto got = download(entry, target, task);
                if (!got) {
                    return std::unexpected(got.error());
                }
                done.push_back(std::move(*got));
                base += entry.size;
            }
            return done;
        },
        [this, post, alive](Result<std::vector<core::DownloadedUpdate>> result) {
            post([this, alive, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                finish();
                if (!result) {
                    if (result.error().code == ErrorCode::Cancelled) {
                        log::info("updates", L"download stopped by the user (the .part files stay for a resume)");
                        if (m_events.stopped) {
                            m_events.stopped();
                        }
                        return;
                    }
                    log::error("updates", describe(result.error()));
                    if (m_events.failed) {
                        m_events.failed(result.error(), true);
                    }
                    return;
                }
                if (m_events.downloaded) {
                    m_events.downloaded(std::move(*result));
                }
            });
        });
}

void UpdateCatalogController::cancel() {
    if (const auto& fetch = m_state.updateFetch()) {
        fetch->cancel.cancel();
    }
}

} // namespace wl::app
