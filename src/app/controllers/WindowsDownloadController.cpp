#include "app/controllers/WindowsDownloadController.h"

#include "base/Log.h"
#include "base/Text.h"
#include "core/image/dism/Dism.h"
#include "core/uup/UupApps.h"
#include "core/uup/UupDownload.h"
#include "ui/anim/Tween.h"

#include <atomic>
#include <format>
#include <regex>

namespace wl::app {

namespace {

// Calls `fn` on the UI thread while the controller lives.
template <class Fn>
void onUi(const std::function<void(std::function<void()>)>& post, std::weak_ptr<bool> alive, Fn fn) {
    post([alive = std::move(alive), fn = std::move(fn)]() mutable {
        if (const auto a = alive.lock(); a && *a) {
            fn();
        }
    });
}

} // namespace

WindowsDownloadController::WindowsDownloadController(AppState& state, Events events)
    : m_state(state), m_events(std::move(events)), m_lists(std::make_unique<core::TaskRunner>()),
      m_net(std::make_unique<core::TaskRunner>()) {}

WindowsDownloadController::~WindowsDownloadController() {
    *m_alive = false;
    if (const auto& job = m_state.windowsDownload()) {
        job->cancel.cancel();
    }
    m_net.reset();
    m_lists.reset();
}

void WindowsDownloadController::listBuilds(std::wstring search) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_lists->run<std::vector<core::uup::Build>>(
        [search](const core::TaskContext& t) { return core::uup::listBuilds(search, t.cancel); },
        [this, post, alive](Result<std::vector<core::uup::Build>> r) {
            onUi(post, alive, [this, r = std::move(r)]() mutable {
                if (!r) {
                    if (m_events.failed) {
                        m_events.failed(r.error(), false);
                    }
                    return;
                }
                if (m_events.builds) {
                    m_events.builds(std::move(*r));
                }
            });
        });
}

void WindowsDownloadController::listLanguages(std::wstring id) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_lists->run<std::vector<core::uup::Language>>(
        [id](const core::TaskContext& t) { return core::uup::listLanguages(id, t.cancel); },
        [this, post, alive, id](Result<std::vector<core::uup::Language>> r) {
            onUi(post, alive, [this, id, r = std::move(r)]() mutable {
                if (!r) {
                    if (m_events.failed) {
                        m_events.failed(r.error(), false);
                    }
                    return;
                }
                if (m_events.languages) {
                    m_events.languages(id, std::move(*r));
                }
            });
        });
}

void WindowsDownloadController::listEditions(std::wstring id, std::wstring language) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_lists->run<std::vector<core::uup::Edition>>(
        [id, language](const core::TaskContext& t) { return core::uup::listEditions(id, language, t.cancel); },
        [this, post, alive, id, language](Result<std::vector<core::uup::Edition>> r) {
            onUi(post, alive, [this, id, language, r = std::move(r)]() mutable {
                if (!r) {
                    if (m_events.failed) {
                        m_events.failed(r.error(), false);
                    }
                    return;
                }
                if (m_events.editions) {
                    m_events.editions(id, language, std::move(*r));
                }
            });
        });
}

void WindowsDownloadController::listFiles(std::wstring id, std::wstring language, std::vector<std::wstring> editions) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_lists->run<core::uup::FileSet>(
        [id, language, editions](const core::TaskContext& t) {
            return core::uup::listFiles(id, language, editions, /*links=*/false, t.cancel);
        },
        [this, post, alive, id, language, editions](Result<core::uup::FileSet> r) {
            onUi(post, alive, [this, id, language, editions, r = std::move(r)]() mutable {
                if (!r) {
                    if (m_events.failed) {
                        m_events.failed(r.error(), false);
                    }
                    return;
                }
                if (m_events.files) {
                    m_events.files(id, language, editions, std::move(*r));
                }
            });
        });
}

void WindowsDownloadController::listApps(core::uup::Build build, std::wstring language, std::vector<std::wstring> editions) {
    Request where;
    where.build = build;
    where.language = language;
    where.editions = editions;
    const auto folder = setFolder(where);
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_lists->run<std::vector<core::uup::AppFeature>>(
        [build, language, editions, folder](const core::TaskContext& t) {
            return core::uup::setApps(build.id, language, editions, build.arch, folder, t.cancel);
        },
        [this, post, alive, id = build.id, language, editions](Result<std::vector<core::uup::AppFeature>> r) {
            onUi(post, alive, [this, id, language, editions, r = std::move(r)]() mutable {
                if (!r) {
                    log::warn("uup", L"app list: " + describe(r.error()));
                    r = std::vector<core::uup::AppFeature>{}; // the picker says there is nothing to pick
                }
                if (m_events.apps) {
                    m_events.apps(id, language, editions, std::move(*r));
                }
            });
        });
}

std::filesystem::path WindowsDownloadController::setFolder(const Request& request) const {
    std::wstring name = std::format(L"{}_{}_{}", request.build.build, request.build.arch, text::lower(request.language));
    for (const auto& e : request.editions) {
        name += L"_" + text::lower(e);
    }
    return m_state.settings().workRoot / L"uup" / name;
}

std::vector<core::uup::File> WindowsDownloadController::filesFor(const core::uup::FileSet& set, bool updates) {
    std::vector<core::uup::File> files;
    for (const auto& f : set.files) {
        const std::wstring lower = text::lower(f.name);
        const bool update = lower.starts_with(L"windows1") && lower.find(L"-kb") != std::wstring::npos;
        if (f.kind == core::uup::FileKind::App || (!updates && (update || f.kind == core::uup::FileKind::Edge))) {
            continue;
        }
        files.push_back(f);
    }
    return files;
}

std::wstring WindowsDownloadController::isoName(const core::uup::Build& build, std::wstring_view language) {
    // "Windows 11, version 26H2 (26300.9550)" → Win11_26H2; an Insider build → Win11_Insider.
    std::wstring windows = L"Windows";
    std::wstring release;
    static const std::wregex version{LR"(Windows (\d+), version (\w+))"};
    static const std::wregex insider{LR"(Windows (\d+) Insider)"};
    std::wsmatch m;
    if (std::regex_search(build.title, m, version)) {
        windows = L"Win" + m[1].str();
        release = L"_" + m[2].str();
    } else if (std::regex_search(build.title, m, insider)) {
        windows = L"Win" + m[1].str();
        release = L"_Insider";
    }
    const std::wstring arch = build.arch == L"amd64" ? L"x64" : build.arch;
    return std::format(L"{}{}_{}_{}_{}.iso", windows, release, build.build, text::lower(language), arch);
}

void WindowsDownloadController::finish() {
    m_state.setWindowsDownload(std::nullopt);
}

void WindowsDownloadController::start(Request request) {
    if (running() || request.editions.empty()) {
        return;
    }
    AppState::WindowsDownload job;
    job.stage = AppState::WindowsDownload::Stage::Preparing;
    job.title = request.build.title;
    job.output = request.output;
    job.startedMs = ui::nowMs();
    const core::CancelToken cancel = job.cancel;
    m_state.setWindowsDownload(std::move(job));
    const std::filesystem::path folder = setFolder(request);
    log::info("uup", std::format(L"Windows download: {} {} {} -> {}", request.build.title, request.language,
                                 request.editions.size(), request.output.wstring()));

    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    auto last = std::make_shared<std::atomic<std::int64_t>>(-1);
    auto report = [this, post, alive, last](double fraction) {
        const auto key = static_cast<std::int64_t>(fraction * 1000);
        if (last->exchange(key) == key) {
            return;
        }
        onUi(post, alive, [this, fraction] {
            if (auto& j = m_state.windowsDownloadMutable()) {
                if (j->stage != AppState::WindowsDownload::Stage::Downloading) {
                    j->stage = AppState::WindowsDownload::Stage::Downloading;
                    j->startedMs = ui::nowMs();
                }
                j->fraction = fraction;
                j->doneBytes = static_cast<std::uint64_t>(fraction * static_cast<double>(j->totalBytes));
                m_state.notifyWindowsDownload();
            }
        });
    };
    m_net->run<bool>(
        [request, folder, cancel, report, post, alive, this](const core::TaskContext&) -> Result<bool> {
            auto set = core::uup::listFiles(request.build.id, request.language, request.editions, /*links=*/true, cancel);
            if (!set) {
                return std::unexpected(set.error());
            }
            auto files = filesFor(*set, request.updates);
            // The apps: which ones the set's app database says (its cabinet first, it is small),
            // downloaded with the rest so one bar shows it all.
            core::uup::AppDownload apps;
            if (request.apps) {
                std::vector<core::uup::File> meta;
                for (const auto& f : files) {
                    if (text::iendsWith(f.name, L".AggregatedMetadata.cab")) {
                        meta.push_back(f);
                    }
                }
                if (auto r = core::uup::downloadFiles(meta, folder, {}, core::TaskContext{cancel, {}}); !r) {
                    return std::unexpected(r.error());
                }
                auto planned = core::uup::planAppDownload(request.build.id, folder, request.editions, request.build.arch, cancel,
                                                          request.excludedApps);
                if (planned) {
                    apps = std::move(*planned);
                    files.insert(files.end(), apps.files.begin(), apps.files.end());
                } else if (planned.error().code == ErrorCode::Cancelled) {
                    return std::unexpected(planned.error());
                } else {
                    log::warn("uup", L"no Store apps: " + describe(planned.error())); // the ISO without them
                }
            }
            std::uint64_t total = 0;
            for (const auto& f : files) {
                total += f.size;
            }
            onUi(post, alive, [this, total] {
                if (auto& j = m_state.windowsDownloadMutable()) {
                    j->totalBytes = total;
                    m_state.notifyWindowsDownload();
                }
            });
            auto refresh = [&]() -> Result<std::vector<core::uup::File>> {
                auto again = core::uup::listFiles(request.build.id, request.language, request.editions, true, cancel);
                if (!again) {
                    return std::unexpected(again.error());
                }
                if (!apps.files.empty()) {
                    if (auto more = core::uup::planAppDownload(request.build.id, folder, request.editions,
                                                               request.build.arch, cancel, request.excludedApps)) {
                        again->files.insert(again->files.end(), more->files.begin(), more->files.end());
                    }
                }
                return again->files;
            };
            const core::TaskContext task{cancel, [&](double f, std::wstring_view) { report(f); }};
            if (auto r = core::uup::downloadFiles(std::move(files), folder, refresh, task); !r) {
                return std::unexpected(r.error());
            }
            return true;
        },
        [this, post, alive, request, folder](Result<bool> r) {
            onUi(post, alive, [this, request, folder, r = std::move(r)]() mutable {
                if (!r) {
                    finish();
                    if (r.error().code == ErrorCode::Cancelled) {
                        if (m_events.stopped) {
                            m_events.stopped();
                        }
                        return;
                    }
                    log::error("uup", describe(r.error()));
                    if (m_events.failed) {
                        m_events.failed(r.error(), true);
                    }
                    return;
                }
                convert(std::move(request), folder);
            });
        });
}

void WindowsDownloadController::convert(Request request, std::filesystem::path folder) {
    auto& job = m_state.windowsDownloadMutable();
    if (!job) {
        return;
    }
    job->stage = AppState::WindowsDownload::Stage::Converting;
    job->fraction = 0;
    job->startedMs = ui::nowMs();
    const core::CancelToken cancel = job->cancel;
    m_state.notifyWindowsDownload();

    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    auto last = std::make_shared<std::atomic<std::int64_t>>(-1);
    auto report = [this, post, alive, last](double fraction, std::wstring step) {
        const auto key = static_cast<std::int64_t>(fraction * 1000);
        if (last->exchange(key) == key) {
            return;
        }
        onUi(post, alive, [this, fraction, step = std::move(step)] {
            if (auto& j = m_state.windowsDownloadMutable()) {
                j->fraction = fraction;
                j->step = step;
                m_state.notifyWindowsDownload();
            }
        });
    };
    core::uup::ConvertOptions options;
    options.uupFolder = folder;
    options.workFolder = m_state.settings().workRoot / L"uup" / L"work";
    options.mediaFolder = m_state.settings().workRoot / L"uup" / L"media";
    options.output = request.output;
    options.updates = request.updates;
    options.edge = request.edge;
    options.apps = request.apps;
    options.excludedApps = request.excludedApps;
    options.netFx3 = request.netFx3;
    options.resetBase = request.resetBase;
    options.compression = request.esd ? core::WimCompression::Lzms : core::WimCompression::Lzx;
    m_state.engine().run<core::uup::ConvertResult>(
        [options, cancel, report](const core::TaskContext&) -> Result<core::uup::ConvertResult> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            const core::TaskContext task{cancel, [&](double f, std::wstring_view s) { report(f, std::wstring(s)); }};
            return core::uup::convertUup(**dism, options, task);
        },
        [this, post, alive, folder](Result<core::uup::ConvertResult> r) {
            onUi(post, alive, [this, folder, r = std::move(r)]() mutable {
                finish();
                if (!r) {
                    if (r.error().code == ErrorCode::Cancelled) {
                        if (m_events.stopped) {
                            m_events.stopped();
                        }
                        return;
                    }
                    log::error("uup", describe(r.error()));
                    if (m_events.failed) {
                        m_events.failed(r.error(), true);
                    }
                    return;
                }
                // The set did its job: ~9 GB back. (A failed run keeps it: the next one continues.)
                std::error_code ec;
                std::filesystem::remove_all(folder, ec);
                if (m_events.finished) {
                    m_events.finished(*r);
                }
            });
        });
}

void WindowsDownloadController::cancel() {
    if (const auto& job = m_state.windowsDownload()) {
        job->cancel.cancel();
    }
}

} // namespace wl::app
