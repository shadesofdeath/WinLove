#include "app/controllers/LanguageFetchController.h"

#include "base/Log.h"

#include <atomic>
#include <format>

namespace wl::app {

std::wstring LanguageTarget::key() const {
    return std::format(L"{}.{}-{}", build, revision, architecture);
}

LanguageFetchController::Backend LanguageFetchController::uupBackend() {
    return Backend{
        [](const LanguageTarget& target, const core::CancelToken& cancel) -> Result<std::vector<core::UupLanguage>> {
            auto build = core::findUupBuild(target.build, target.revision, target.architecture, cancel);
            if (!build) {
                return std::unexpected(build.error());
            }
            auto files = core::uupFiles(build->uuid, cancel);
            if (!files) {
                return std::unexpected(files.error());
            }
            return core::uupLanguages(*files, target.architecture);
        },
        [](const std::vector<core::UupLanguageFile>& files, const std::filesystem::path& folder, const core::TaskContext& task) {
            return core::downloadUupFiles(files, folder, task);
        },
    };
}

LanguageFetchController::LanguageFetchController(AppState& state, Events events, Backend backend)
    : m_state(state), m_events(std::move(events)), m_backend(std::move(backend)), m_net(std::make_unique<core::TaskRunner>()) {}

LanguageFetchController::~LanguageFetchController() {
    *m_alive = false;
    if (const auto& fetch = m_state.languageFetch()) {
        fetch->cancel.cancel(); // the runner joins the running job below
    }
    m_net.reset();
}

std::optional<LanguageTarget> LanguageFetchController::targetFor(const AppState& state) {
    const auto& mounted = state.mounted();
    const auto& source = state.source();
    if (!mounted || !source) {
        return std::nullopt;
    }
    for (const auto& image : source->install.images) {
        if (image.index == mounted->index && image.build > 0) {
            return LanguageTarget{image.build, image.spBuild, core::architectureName(image.architecture)};
        }
    }
    return std::nullopt;
}

std::filesystem::path LanguageFetchController::folder(const LanguageTarget& target) const {
    return m_state.settings().workRoot / L"languages" / target.key();
}

void LanguageFetchController::find() {
    const auto target = targetFor(m_state);
    if (busy() || !target) {
        return;
    }
    if (const auto it = m_cache.find(target->key()); it != m_cache.end()) {
        if (m_events.offers) {
            m_events.offers(*target, it->second);
        }
        return;
    }
    AppState::LanguageFetch fetch;
    const core::CancelToken cancel = fetch.cancel;
    m_state.setLanguageFetch(std::move(fetch));
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_net->run<std::vector<core::UupLanguage>>(
        [find = m_backend.find, target = *target, cancel](const core::TaskContext&) { return find(target, cancel); },
        [this, post, alive, target = *target](Result<std::vector<core::UupLanguage>> result) {
            post([this, alive, target, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_state.setLanguageFetch(std::nullopt);
                if (!result) {
                    if (result.error().code != ErrorCode::Cancelled) {
                        log::error("languages", describe(result.error()));
                        if (m_events.failed) {
                            m_events.failed(result.error(), false);
                        }
                    }
                    return;
                }
                log::info("languages", std::format(L"{} language(s) on offer for {}", result->size(), target.key()));
                const auto& cached = m_cache[target.key()] = std::move(*result);
                if (m_events.offers) {
                    m_events.offers(target, cached);
                }
            });
        });
}

void LanguageFetchController::download(std::vector<core::UupLanguageFile> files) {
    const auto target = targetFor(m_state);
    if (busy() || files.empty() || !target) {
        return;
    }
    AppState::LanguageFetch fetch;
    fetch.stage = AppState::LanguageFetch::Stage::Downloading;
    for (const auto& f : files) {
        fetch.totalBytes += f.source.size;
        if (!f.file.language.empty() && std::ranges::find(fetch.languages, f.file.language) == fetch.languages.end()) {
            fetch.languages.push_back(f.file.language);
        }
    }
    const core::CancelToken cancel = fetch.cancel;
    const std::uint64_t total = fetch.totalBytes;
    m_state.setLanguageFetch(std::move(fetch));
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // Progress to the UI only when the per-mille changes.
    auto last = std::make_shared<std::atomic<std::int64_t>>(-1);
    auto report = [this, post, alive, last, total](double fraction, bool verifying) {
        const auto done = static_cast<std::uint64_t>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(total));
        const std::int64_t key = (verifying ? 1000 : 0) + (total ? static_cast<std::int64_t>(done * 999 / total) : 0);
        if (last->exchange(key) == key) {
            return;
        }
        post([this, alive, done, verifying] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            if (auto& f = m_state.languageFetchMutable()) {
                f->doneBytes = done;
                f->stage = verifying ? AppState::LanguageFetch::Stage::Verifying : AppState::LanguageFetch::Stage::Downloading;
                m_state.notifyLanguageFetch();
            }
        });
    };
    m_net->run<std::vector<std::filesystem::path>>(
        [download = m_backend.download, files = std::move(files), dir = folder(*target), cancel,
         report](const core::TaskContext&) -> Result<std::vector<std::filesystem::path>> {
            const core::TaskContext task{cancel, [&](double fraction, std::wstring_view stage) {
                                             if (fraction >= 0) {
                                                 report(fraction, stage == L"verify");
                                             }
                                         }};
            return download(files, dir, task);
        },
        [this, post, alive](Result<std::vector<std::filesystem::path>> result) {
            post([this, alive, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_state.setLanguageFetch(std::nullopt);
                if (!result) {
                    if (result.error().code == ErrorCode::Cancelled) {
                        log::info("languages", L"download stopped by the user (the .part files stay for a resume)");
                        if (m_events.stopped) {
                            m_events.stopped();
                        }
                        return;
                    }
                    log::error("languages", describe(result.error()));
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

void LanguageFetchController::cancel() {
    if (const auto& fetch = m_state.languageFetch()) {
        fetch->cancel.cancel();
    }
}

} // namespace wl::app
