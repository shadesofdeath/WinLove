#include "app/controllers/IsoController.h"

#include "app/controllers/UnattendController.h"
#include "base/Log.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"

#include <atomic>
#include <cmath>
#include <format>

namespace wl::app {

using Run = AppState::IsoRun;

namespace {
bool isInside(const std::filesystem::path& path, const std::filesystem::path& folder) {
    const std::wstring p = path.lexically_normal().wstring();
    const std::wstring f = folder.lexically_normal().wstring();
    return p.size() > f.size() && _wcsnicmp(p.c_str(), f.c_str(), f.size()) == 0 && (p[f.size()] == L'\\' || p[f.size()] == L'/');
}

core::WimCompression compressionOf(IsoController::Repack r) {
    switch (r) {
    case IsoController::Repack::Xpress: return core::WimCompression::Xpress;
    case IsoController::Repack::Esd: return core::WimCompression::Lzms;
    default: return core::WimCompression::Lzx;
    }
}
} // namespace

IsoController::IsoController(AppState& state, Events events) : m_state(state), m_events(std::move(events)) {}

IsoController::~IsoController() {
    *m_alive = false;
}

std::optional<IsoController::Blocker> IsoController::blocker() const {
    const auto& source = m_state.source();
    if (!source) {
        return Blocker::NoSource;
    }
    if (source->format != core::ImageFormat::Folder && source->format != core::ImageFormat::Iso) {
        return Blocker::WimOnly;
    }
    if (m_state.mounted()) {
        return Blocker::Mounted;
    }
    const auto& apply = m_state.applyRun();
    if (m_state.operation() || running() || (apply && apply->stage != AppState::ApplyRun::Stage::Done)) {
        return Blocker::Busy;
    }
    if (m_state.unattend().includeInIso && !core::validateUnattend(UnattendController::effective(m_state)).empty()) {
        return Blocker::UnattendInvalid;
    }
    return std::nullopt;
}

bool IsoController::canRepack() const {
    const auto& source = m_state.source();
    if (!source) {
        return false;
    }
    // ISO sources are extracted into the work folder first, so they qualify too.
    const auto legacyWork = AppSettings::legacyMountDirectory().parent_path(); // C:\WinLove (before 2026-09-28)
    return source->format == core::ImageFormat::Iso || isInside(source->path, m_state.settings().workRoot) ||
           isInside(source->path, legacyWork);
}

bool IsoController::running() const {
    const auto& run = m_state.isoRun();
    return run && run->running;
}

void IsoController::start(Request request) {
    if (blocker()) {
        return;
    }
    const core::SourceInfo source = *m_state.source();
    if (request.repack != Repack::AsIs && !canRepack()) {
        request.repack = Repack::AsIs;
    }
    const std::filesystem::path workFolder = m_state.settings().workDirectoryFor(source.path);
    const std::string answerFile = UnattendController::isoFile(m_state); // empty: not asked for
    Run run;
    run.running = true;
    run.output = request.output;
    run.startedMs = 0;
    const core::CancelToken cancel = run.cancel;
    m_state.setIsoRun(std::move(run));
    log::info("iso", L"ISO build started: " + request.output.wstring());

    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    auto lastPercent = std::make_shared<std::atomic<int>>(-1);
    auto report = [post, alive, lastPercent, this](double fraction, int stage) {
        const int percent = static_cast<int>(std::floor(fraction * 100));
        if (lastPercent->exchange(percent) == percent) {
            return;
        }
        post([alive, this, fraction, stage] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            if (auto& r = m_state.isoRunMutable()) {
                r->fraction = fraction;
                r->stage = stage;
                m_state.notifyIso();
            }
        });
    };

    m_state.engine().run<core::IsoResult>(
        [source, workFolder, request, cancel, report, answerFile](const core::TaskContext&) -> Result<core::IsoResult> {
            // Weights: extract 0.35 (ISO sources), repack 0.35 (if asked), build the rest.
            const bool extract = source.format == core::ImageFormat::Iso;
            const bool repack = request.repack != Repack::AsIs;
            const double we = extract ? 0.35 : 0.0;
            const double wr = repack ? 0.35 : 0.0;
            const double wb = 1.0 - we - wr;
            std::filesystem::path folder = source.path;
            if (extract) {
                auto iso = core::UdfImage::open(source.path);
                if (!iso) {
                    return std::unexpected(iso.error());
                }
                const core::TaskContext t{cancel, [&](double f, std::wstring_view) { report(f * we, 0); }};
                if (auto r = iso->extractAll(workFolder, t); !r) {
                    return std::unexpected(r.error());
                }
                folder = workFolder;
            }
            if (repack) {
                const core::TaskContext t{cancel, [&](double f, std::wstring_view) { report(we + f * wr, 1); }};
                if (auto r = core::repackInstallImage(folder, compressionOf(request.repack), t); !r) {
                    return std::unexpected(r.error());
                }
            }
            core::IsoOptions options;
            options.sourceFolder = folder;
            options.output = request.output;
            options.volumeLabel = request.label;
            options.boot = request.boot;
            options.noPrompt = request.noPrompt;
            options.writeSha256 = request.sha256;
            if (!answerFile.empty()) {
                options.rootFiles.push_back({L"autounattend.xml", answerFile});
            }
            const core::TaskContext t{cancel, [&](double f, std::wstring_view stage) {
                                          report(we + wr + f * wb, stage == L"sha256" ? 3 : 2);
                                      }};
            return core::buildIso(options, t);
        },
        [this, post, alive, request](Result<core::IsoResult> result) {
            post([this, alive, request, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                auto& run = m_state.isoRunMutable();
                if (!run) {
                    return;
                }
                run->running = false;
                if (!result) {
                    log::error("iso", describe(result.error()));
                    run->error = result.error();
                    m_state.notifyIso();
                    if (result.error().code != ErrorCode::Cancelled && m_events.failed) {
                        m_events.failed(result.error());
                    }
                    return;
                }
                run->fraction = 1.0;
                run->result = *result;
                m_state.notifyIso();
                if (m_events.finished) {
                    m_events.finished(*result, request.output, request.openFolder);
                }
            });
        },
        {});
}

void IsoController::cancel() {
    if (const auto& run = m_state.isoRun(); run && run->running) {
        run->cancel.cancel();
    }
}

} // namespace wl::app
