#include "app/controllers/WindowsDownloadController.h"

#include "base/Log.h"
#include "base/Text.h"
#include "core/esd/EsdConvert.h"
#include "core/image/dism/Dism.h"
#include "core/system/Privileges.h"
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

// The request before `next` in the same list is of no use any more (audit CTL-14).
void supersede(std::optional<core::CancelToken>& last, core::CancelToken next) {
    if (last) {
        last->cancel();
    }
    last = std::move(next);
}

// A superseded request: nothing failed, its answer is simply not wanted.
bool superseded(const Error& error) noexcept {
    return error.code == ErrorCode::Cancelled;
}

} // namespace

WindowsDownloadController::WindowsDownloadController(AppState& state, Events events)
    : m_state(state), m_events(std::move(events)), m_lists(std::make_unique<core::TaskRunner>()),
      m_appLists(std::make_unique<core::TaskRunner>()), m_catalogs(std::make_unique<core::TaskRunner>()),
      m_net(std::make_unique<core::TaskRunner>()) {}

WindowsDownloadController::~WindowsDownloadController() {
    *m_alive = false;
    if (const auto& job = m_state.windowsDownload()) {
        job->cancel.cancel();
    }
    m_net.reset();
    m_catalogs.reset();
    m_appLists.reset();
    m_lists.reset();
}

void WindowsDownloadController::listBuilds(std::wstring search) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    supersede(m_buildsRequest, m_lists->run<std::vector<core::uup::Build>>(
        [search](const core::TaskContext& t) { return core::uup::listBuilds(search, t.cancel); },
        [this, post, alive](Result<std::vector<core::uup::Build>> r) {
            onUi(post, alive, [this, r = std::move(r)]() mutable {
                if (!r) {
                    if (superseded(r.error())) {
                        return;
                    }
                    if (m_events.failed) {
                        m_events.failed(r.error(), Failed::List);
                    }
                    return;
                }
                if (m_events.builds) {
                    m_events.builds(std::move(*r));
                }
            });
        }));
}

void WindowsDownloadController::listLanguages(std::wstring id) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    supersede(m_languagesRequest, m_lists->run<std::vector<core::uup::Language>>(
        [id](const core::TaskContext& t) { return core::uup::listLanguages(id, t.cancel); },
        [this, post, alive, id](Result<std::vector<core::uup::Language>> r) {
            onUi(post, alive, [this, id, r = std::move(r)]() mutable {
                if (!r) {
                    if (superseded(r.error())) {
                        return;
                    }
                    if (m_events.failed) {
                        m_events.failed(r.error(), Failed::List);
                    }
                    return;
                }
                if (m_events.languages) {
                    m_events.languages(id, std::move(*r));
                }
            });
        }));
}

void WindowsDownloadController::listEditions(std::wstring id, std::wstring language) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    supersede(m_editionsRequest, m_lists->run<std::vector<core::uup::Edition>>(
        [id, language](const core::TaskContext& t) { return core::uup::listEditions(id, language, t.cancel); },
        [this, post, alive, id, language](Result<std::vector<core::uup::Edition>> r) {
            onUi(post, alive, [this, id, language, r = std::move(r)]() mutable {
                if (!r) {
                    if (superseded(r.error())) {
                        return;
                    }
                    if (m_events.failed) {
                        m_events.failed(r.error(), Failed::List);
                    }
                    return;
                }
                if (m_events.editions) {
                    m_events.editions(id, language, std::move(*r));
                }
            });
        }));
}

void WindowsDownloadController::listFiles(std::wstring id, std::wstring language, std::vector<std::wstring> editions) {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // The Store apps' set ("neutral") is asked for next to the selection's: each has its own turn.
    auto& last = language == L"neutral" ? m_appFilesRequest : m_filesRequest;
    supersede(last, m_lists->run<core::uup::FileSet>(
        [id, language, editions](const core::TaskContext& t) {
            return core::uup::listFiles(id, language, editions, /*links=*/false, t.cancel);
        },
        [this, post, alive, id, language, editions](Result<core::uup::FileSet> r) {
            onUi(post, alive, [this, id, language, editions, r = std::move(r)]() mutable {
                if (!r) {
                    if (superseded(r.error())) {
                        return;
                    }
                    if (m_events.failed) {
                        m_events.failed(r.error(), Failed::List);
                    }
                    return;
                }
                if (m_events.files) {
                    m_events.files(id, language, editions, std::move(*r));
                }
            });
        }));
}

void WindowsDownloadController::listApps(core::uup::Build build, std::wstring language, std::vector<std::wstring> editions) {
    Request where;
    where.build = build;
    where.language = language;
    where.editions = editions;
    const auto folder = setFolder(where);
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    supersede(m_appsRequest, m_appLists->run<std::vector<core::uup::AppFeature>>(
        [build, language, editions, folder](const core::TaskContext& t) {
            return core::uup::setApps(build.id, language, editions, build.arch, folder, t.cancel);
        },
        [this, post, alive, id = build.id, language, editions](Result<std::vector<core::uup::AppFeature>> r) {
            onUi(post, alive, [this, id, language, editions, r = std::move(r)]() mutable {
                if (!r && superseded(r.error())) {
                    return; // not "nothing to pick": the request after it brings the list
                }
                if (!r) {
                    log::warn("uup", L"app list: " + describe(r.error()));
                    r = std::vector<core::uup::AppFeature>{}; // the picker says there is nothing to pick
                }
                if (m_events.apps) {
                    m_events.apps(id, language, editions, std::move(*r));
                }
            });
        }));
}

void WindowsDownloadController::listEsdCatalogs() {
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    for (const auto product : {core::esd::Product::Windows11, core::esd::Product::Windows10}) {
        const auto slot = static_cast<std::size_t>(product);
        if (m_esdCatalogs[slot]) {
            continue; // the page reads it with esdCatalog()
        }
        if (m_esdAsked[slot]) {
            continue; // on its way
        }
        m_esdAsked[slot] = true;
        (void)m_catalogs->run<core::esd::Catalog>(
            [product](const core::TaskContext& t) {
                return core::esd::fetchCatalog(product, L"WinLove/" WL_VERSION_STRING, t.cancel);
            },
            [this, post, alive, product, slot](Result<core::esd::Catalog> r) {
                onUi(post, alive, [this, product, slot, r = std::move(r)]() mutable {
                    m_esdAsked[slot] = false;
                    if (r) {
                        log::info("esd", std::format(L"official catalog: {} entries, build {} ({})", r->entries.size(), r->build(),
                                                     r->source));
                        m_esdCatalogs[slot] = *r;
                    } else {
                        log::warn("esd", L"official catalog: " + describe(r.error()));
                    }
                    if (m_events.esdCatalog) {
                        m_events.esdCatalog(product, std::move(r));
                    }
                });
            });
    }
}

std::filesystem::path WindowsDownloadController::esdFolder() const {
    return m_state.settings().workRoot / L"esd";
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
        const bool update = core::uup::isUpdatePackage(f.name);
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

std::wstring WindowsDownloadController::isoName(const core::uup::Build& build, std::wstring_view language,
                                               std::wstring_view esdBuild) {
    core::uup::Build named = build;
    if (!esdBuild.empty() && esdBuild != build.build) {
        // "Windows 11, version 26H2 (26300.9550)" with the ESD's own build in the name.
        const auto at = named.title.rfind(build.build);
        if (at != std::wstring::npos) {
            named.title.replace(at, build.build.size(), esdBuild);
        }
        named.build = std::wstring(esdBuild);
    }
    return isoName(named, language);
}

void WindowsDownloadController::finish() {
    m_state.setWindowsDownload(std::nullopt);
}

void WindowsDownloadController::start(Request request) {
    if (running() || request.editions.empty()) {
        return;
    }
    if (request.source == core::esd::Source::Esd) {
        startEsd(std::move(request));
        return;
    }
    AppState::WindowsDownload job;
    job.stage = AppState::WindowsDownload::Stage::Preparing;
    job.title = request.build.title;
    job.output = request.output;
    job.startedMs = ui::nowMs();
    const core::CancelToken cancel = job.cancel;
    m_state.setWindowsDownloadNotice(std::nullopt);
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
                        m_events.failed(r.error(), Failed::Download);
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
            auto converted = core::uup::convertUup(**dism, options, task);
            if (converted) {
                // The set did its job: ~9 GB back. (A failed run keeps it: the next one continues.)
                // Here, not on the UI thread: hundreds of files, seconds on a slow disk (audit CTL-15).
                std::error_code ec;
                std::filesystem::remove_all(options.uupFolder, ec);
            }
            return converted;
        },
        [this, post, alive](Result<core::uup::ConvertResult> r) {
            onUi(post, alive, [this, r = std::move(r)]() mutable {
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
                        m_events.failed(r.error(), Failed::Convert);
                    }
                    return;
                }
                if (m_events.finished) {
                    m_events.finished(*r);
                }
            });
        });
}

void WindowsDownloadController::startEsd(Request request) {
    if (request.esdFiles.empty()) {
        return;
    }
    AppState::WindowsDownload job;
    job.stage = AppState::WindowsDownload::Stage::Preparing;
    job.esd = true;
    job.step = request.esdFiles.front().sha256.empty() ? L"SHA-1" : L"SHA-256"; // what "Doğrulanıyor" names
    job.title = request.build.title;
    job.output = request.output;
    job.startedMs = ui::nowMs();
    for (const auto& f : request.esdFiles) {
        job.totalBytes += f.size;
    }
    const core::CancelToken cancel = job.cancel;
    m_state.setWindowsDownloadNotice(std::nullopt);
    m_state.setWindowsDownload(std::move(job));
    const std::filesystem::path folder = esdFolder();
    log::info("esd", std::format(L"Windows download (official ESD): {} {} {} file(s) -> {}", request.build.title, request.language,
                                 request.esdFiles.size(), request.output.wstring()));

    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    auto last = std::make_shared<std::atomic<std::int64_t>>(-1);
    auto report = [this, post, alive, last](double fraction, std::wstring stage) {
        const bool verify = stage == L"verify";
        const auto key = static_cast<std::int64_t>(fraction * 1000) + (verify ? 100000 : 0);
        if (last->exchange(key) == key) {
            return;
        }
        onUi(post, alive, [this, fraction, verify] {
            if (auto& j = m_state.windowsDownloadMutable()) {
                const auto stage = verify ? AppState::WindowsDownload::Stage::Verifying : AppState::WindowsDownload::Stage::Downloading;
                if (j->stage != stage) {
                    j->stage = stage;
                    j->startedMs = ui::nowMs();
                }
                j->fraction = fraction;
                if (!verify) {
                    j->doneBytes = static_cast<std::uint64_t>(fraction * static_cast<double>(j->totalBytes));
                }
                m_state.notifyWindowsDownload();
            }
        });
    };
    m_net->run<std::vector<std::filesystem::path>>(
        [files = request.esdFiles, folder, cancel, report](const core::TaskContext&) {
            const core::TaskContext task{cancel, [&](double f, std::wstring_view s) { report(f, std::wstring(s)); }};
            return core::esd::downloadFiles(files, folder, L"WinLove/" WL_VERSION_STRING, task);
        },
        [this, post, alive, request](Result<std::vector<std::filesystem::path>> r) {
            onUi(post, alive, [this, request, r = std::move(r)]() mutable {
                if (!r) {
                    finish();
                    if (r.error().code == ErrorCode::Cancelled) {
                        if (m_events.stopped) {
                            m_events.stopped();
                        }
                        return;
                    }
                    log::error("esd", describe(r.error()));
                    if (m_events.failed) {
                        m_events.failed(r.error(), Failed::Download);
                    }
                    return;
                }
                convertEsd(std::move(request), std::move(*r));
            });
        });
}

void WindowsDownloadController::convertEsd(Request request, std::vector<std::filesystem::path> files) {
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
    core::esd::ConvertOptions options;
    options.esds = std::move(files);
    options.editions = request.editions;
    options.workFolder = esdFolder() / L"work";
    options.mediaFolder = esdFolder() / L"media";
    options.output = request.output;
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
            auto converted = core::esd::convertEsd(**dism, options, task);
            if (converted) {
                // The ESD did its job (~6 GB back); a failed run keeps it for the next one.
                std::error_code ec;
                for (const auto& f : options.esds) {
                    std::filesystem::remove(f, ec);
                }
            }
            return converted;
        },
        [this, post, alive](Result<core::uup::ConvertResult> r) {
            onUi(post, alive, [this, r = std::move(r)]() mutable {
                finish();
                if (!r) {
                    if (r.error().code == ErrorCode::Cancelled) {
                        if (m_events.stopped) {
                            m_events.stopped();
                        }
                        return;
                    }
                    log::error("esd", describe(r.error()));
                    if (m_events.failed) {
                        m_events.failed(r.error(), Failed::Convert);
                    }
                    return;
                }
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

void WindowsDownloadController::releaseLeftovers() {
    std::error_code ec;
    // D-149: the official ESD's conversion has a work folder of its own.
    for (const auto& work : {m_state.settings().workRoot / L"uup" / L"work", esdFolder() / L"work"}) {
        if (!core::isElevated() || !std::filesystem::exists(work / L"mount", ec)) {
            continue; // a finished conversion removes its work folder
        }
        m_state.engine().run<bool>(
            [work](const core::TaskContext&) -> Result<bool> {
                auto dism = core::Dism::instance();
                if (!dism) {
                    return std::unexpected(dism.error());
                }
                return core::uup::releaseWorkMount(**dism, work);
            },
            [](Result<bool> released) {
                if (!released) {
                    log::warn("uup", L"the mount an earlier conversion left: " + describe(released.error()));
                } else if (*released) {
                    log::info("uup", L"the mount an earlier conversion left is gone");
                }
            },
            {});
    }
}

} // namespace wl::app
