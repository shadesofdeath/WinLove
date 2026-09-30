#include "app/controllers/IsoController.h"

#include "app/controllers/UnattendController.h"
#include "base/Log.h"
#include "core/image/Source.h"
#include "core/image/UdfImage.h"
#include "core/image/dism/Dism.h"

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

Result<core::BootPatchReport> patchWithDism(const std::filesystem::path& bootWim, const std::filesystem::path& mountDir,
                                            const core::BootPatch& patch, const core::TaskContext& task) {
    auto dism = core::Dism::instance();
    if (!dism) {
        return std::unexpected(dism.error());
    }
    return core::patchBootImage(**dism, bootWim, mountDir, patch, task);
}

// Deletes the patched copy when the build is over, however it ends.
struct ScratchFile {
    std::filesystem::path file;
    ScratchFile() = default;
    ScratchFile(const ScratchFile&) = delete;
    ScratchFile& operator=(const ScratchFile&) = delete;
    ~ScratchFile() {
        if (!file.empty()) {
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
        }
    }
};
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

core::BootPatch IsoController::bootPatch(const AppState& state) {
    const core::UnattendOptions& answers = state.unattend().options;
    core::BootPatch patch;
    patch.bypassTpm = answers.bypassTpm;
    patch.bypassSecureBoot = answers.bypassSecureBoot;
    patch.bypassRam = answers.bypassRam;
    patch.bypassCpu = answers.bypassCpu;
    patch.bypassStorage = answers.bypassStorage;
    return patch;
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
    const core::BootPatch boot = request.bootBypass ? bootPatch(m_state) : core::BootPatch{};
    const std::filesystem::path bootFolder = m_state.settings().workRoot / L"boot"; // the copy and its mount folder
    const BootPatcher patcher = m_patcher ? m_patcher : BootPatcher{patchWithDism};
    Run run;
    run.running = true;
    run.output = request.output;
    run.startedMs = 0;
    const core::CancelToken cancel = run.cancel;
    m_state.setIsoRun(std::move(run));
    log::info("iso", L"ISO build started: " + request.output.wstring());
    // Whether Setup will find an answer file is the first thing to know when it asks its questions anyway.
    if (answerFile.empty()) {
        const bool unused = !(m_state.unattend().options == core::UnattendOptions{});
        log::info("iso", unused ? L"answer file: not added (\"ISO'ya ekle\" is off; the answers on the page stay unused)"
                                : L"answer file: none");
    } else {
        log::info("iso", std::format(L"answer file: autounattend.xml ({} bytes) goes to the ISO root", answerFile.size()));
    }
    if (!boot.empty()) {
        log::info("iso", std::format(L"setup image: {} requirement check(s) are switched off in boot.wim as well",
                                     boot.labConfigValues().size()));
    }

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
        [source, workFolder, request, cancel, report, answerFile, boot, bootFolder,
         patcher](const core::TaskContext&) -> Result<core::IsoResult> {
            // Weights: extract 0.30 (ISO sources), repack 0.30 (if asked), boot image 0.15 (if
            // asked), build the rest.
            const bool extract = source.format == core::ImageFormat::Iso;
            const bool repack = request.repack != Repack::AsIs;
            const double we = extract ? 0.30 : 0.0;
            const double wr = repack ? 0.30 : 0.0;
            const double wp = boot.empty() ? 0.0 : 0.15;
            const double wb = 1.0 - we - wr - wp;
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
            // Setup's own image: patched in a copy, so the setup folder keeps the file it has and
            // the next build starts from that again (nothing to undo when the box is cleared).
            ScratchFile patched; // outlives buildIso, which reads it while writing
            const std::filesystem::path original = folder / L"sources" / L"boot.wim";
            std::error_code ec;
            if (!boot.empty() && !std::filesystem::exists(original, ec)) {
                log::warn("iso", L"no sources\\boot.wim in the setup files: the requirement bypass stays out of Setup's image");
            } else if (!boot.empty()) {
                const std::filesystem::path mountDir = bootFolder / L"mount";
                std::filesystem::create_directories(mountDir, ec);
                if (ec) {
                    return fail(ErrorCode::IoError, L"cannot create folder", mountDir.wstring(), ec.value());
                }
                report(we + wr, 4);
                patched.file = bootFolder / L"boot.wim";
                std::filesystem::copy_file(original, patched.file, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    return fail(ErrorCode::IoError, L"cannot copy boot.wim", patched.file.wstring(), ec.value());
                }
                // Setup files copied off a DVD are read-only; the copy is ours to change.
                std::filesystem::permissions(patched.file, std::filesystem::perms::owner_write,
                                             std::filesystem::perm_options::add, ec);
                const core::TaskContext t{cancel, [&](double f, std::wstring_view) { report(we + wr + f * wp, 4); }};
                const auto done = patcher(patched.file, mountDir, boot, t);
                if (!done) {
                    return std::unexpected(done.error());
                }
                log::info("iso", std::format(L"boot.wim index {}: {} requirement check(s) switched off", done->index,
                                             boot.labConfigValues().size()));
                options.replacedFiles.push_back({L"sources\\boot.wim", patched.file});
            }
            const core::TaskContext t{cancel, [&](double f, std::wstring_view stage) {
                                          report(we + wr + wp + f * wb, stage == L"sha256" ? 3 : 2);
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
