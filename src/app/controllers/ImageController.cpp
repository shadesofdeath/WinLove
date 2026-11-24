#include "app/controllers/ImageController.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/image/UdfImage.h"
#include "core/image/SetupMedia.h"
#include "core/image/Source.h"
#include "core/image/VirtualDisk.h"
#include "core/image/WindowsRelease.h"
#include "core/image/dism/Dism.h"
#include "core/image/dism/DismErrors.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/wim/WimGapi.h"
#include "core/system/Privileges.h"
#include "ui/anim/Tween.h"

#include <cmath>
#include <format>
#include <numeric>

namespace wl::app {

std::filesystem::path sourceForMountedImage(const std::filesystem::path& imagePath) {
    return core::sourceForMountedImage(imagePath); // only real setup media; else the image file (2026-11-11)
}

ImageController::ImageController(AppState& state, Events events) : m_state(state), m_events(std::move(events)) {}

ImageController::~ImageController() {
    *m_alive = false;
}

bool ImageController::busy() const {
    const auto& run = m_state.applyRun(); // an "Uygula" run owns the mounted image
    const auto& iso = m_state.isoRun();   // an ISO build reads the setup folder
    return m_state.operation().has_value() || (run && run->stage != AppState::ApplyRun::Stage::Done) ||
           (iso && iso->running);
}

bool ImageController::isEsdSource() const {
    const auto& source = m_state.source();
    if (!source || source->installImage.size() < 4) {
        return false;
    }
    // Case-insensitive: "WIN11.ESD" is as much an ESD as "install.esd".
    return _wcsicmp(source->installImage.c_str() + source->installImage.size() - 4, L".esd") == 0;
}

bool ImageController::isSolidInstall() const {
    const auto& source = m_state.source();
    if (!source) {
        return false;
    }
    const auto& header = source->install.header;
    return header.solid || header.compression == core::WimCompression::Lzms;
}

bool ImageController::canMount() const {
    return m_state.source() && !isPackedSource() && !m_state.mounted() && !busy();
}

bool ImageController::isVirtualDiskSource() const {
    return m_state.source() && core::isVirtualDiskFormat(m_state.source()->format);
}

std::optional<Str> ImageController::editRefusal() const {
    const auto& source = m_state.source();
    if (!source) {
        return Str::ImagesEmptyTitle;
    }
    if (isVirtualDiskSource()) {
        return Str::ImagesVhdNoWim; // one installed Windows: no WIM edition list to edit
    }
    if (busy()) {
        return Str::ImagesBusy;
    }
    if (m_state.mounted()) {
        return Str::ImagesUnmountFirst;
    }
    // A rewrite is an export and the XML is wimgapi's to write: only a plain WIM stays what it was.
    const auto& header = source->install.header;
    const bool container = source->format == core::ImageFormat::Wim || source->format == core::ImageFormat::Folder ||
                           source->format == core::ImageFormat::Iso;
    if (!container || isPackedSource() || header.solid || header.totalParts > 1 ||
        header.compression == core::WimCompression::Lzms) {
        return Str::ImagesDeleteNeedsWim;
    }
    return std::nullopt;
}

std::optional<Str> ImageController::deleteRefusal() const {
    if (const auto refusal = editRefusal()) {
        return refusal;
    }
    if (m_state.source()->install.images.size() < 2) {
        return Str::ImagesLastEdition;
    }
    return std::nullopt;
}

std::optional<Str> ImageController::verifyRefusal() const {
    const auto& source = m_state.source();
    if (!source) {
        return Str::ImagesEmptyTitle;
    }
    if (isVirtualDiskSource()) {
        return Str::ImagesVhdNoWim;
    }
    if (busy()) {
        return Str::ImagesBusy;
    }
    const auto& header = source->install.header;
    if (isPackedSource() || header.solid || header.compression == core::WimCompression::Lzms) {
        return Str::ImagesVerifyEsd;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ImageController::installWimPath() const {
    const auto& source = m_state.source();
    if (!source) {
        return std::nullopt;
    }
    switch (source->format) {
    case core::ImageFormat::Wim:
    case core::ImageFormat::Esd:
    case core::ImageFormat::Swm: return nativePath(source->path);
    case core::ImageFormat::Folder: return nativePath(source->path / source->installImage);
    default: return std::nullopt; // inside an ISO: not a file yet
    }
}

std::wstring ImageController::editionName(int index) const {
    if (const auto& source = m_state.source()) {
        for (const auto& image : source->install.images) {
            if (image.index == index) {
                return image.name;
            }
        }
    }
    return std::format(L"#{}", index);
}

void ImageController::run(EngineOperation op, Work work, std::function<void()> onSuccess, Failure failure,
                          std::function<void(const Error&)> onFailure) {
    op.startedMs = ui::nowMs();
    const core::CancelToken cancel = op.cancel;
    // The row says "Bağlanamadı" only when a mount (or the work copy made for one) failed — not a
    // delete's copy step (2026-11-11).
    const bool mountWork = op.kind == EngineOperation::Kind::Mounting ||
                           (op.kind == EngineOperation::Kind::Preparing && op.purpose.value_or(op.kind) == EngineOperation::Kind::Mounting);
    const int index = mountWork ? op.index : 0;
    m_state.beginOperation(std::move(op));
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // Progress: engine thread → UI thread, only when the whole percent changes (keeps the queue small).
    auto lastPercent = std::make_shared<std::atomic<int>>(-1);
    core::ProgressFn progress = [this, post, alive, lastPercent](double fraction, std::wstring_view) {
        const int percent = static_cast<int>(std::floor(fraction * 100));
        if (lastPercent->exchange(percent) == percent) {
            return;
        }
        post([this, alive, fraction] {
            if (const auto a = alive.lock(); a && *a) {
                m_state.updateOperation(fraction);
            }
        });
    };
    m_state.engine().run<bool>(
        [work = std::move(work), cancel, progress](const core::TaskContext&) -> Result<bool> {
            core::TaskContext task{cancel, progress};
            if (auto r = work(task); !r) {
                return std::unexpected(r.error());
            }
            return true;
        },
        [this, post, alive, onSuccess = std::move(onSuccess), failure, index, onFailure = std::move(onFailure)](Result<bool> result) {
            post([this, alive, onSuccess, failure, index, onFailure, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_state.endOperation();
                if (!result) {
                    log::error("app", describe(result.error()));
                    if (onFailure) {
                        onFailure(result.error()); // before the toast: the page that shows it is set first
                    }
                    if (result.error().code == ErrorCode::Cancelled) {
                        m_events.refused(Str::ImagesCancelledToast);
                        return;
                    }
                    if (index > 0) {
                        m_failedIndex = index; // the row says "Bağlanamadı": only a mount's failure is that
                    }
                    m_events.failed(failure, result.error());
                    inspectMountFolder(); // show what the failure left behind
                    return;
                }
                m_failedIndex.reset();
                onSuccess();
            });
        },
        {});
}

void ImageController::withWritableSource(int index, EngineOperation::Kind purpose, std::vector<int> rows, std::function<void()> next) {
    const auto& source = m_state.source();
    if (source->format != core::ImageFormat::Iso) {
        next();
        return;
    }
    // DISM and wimgapi need real files: the ISO is copied to its work folder (WorkCopy.h). A copy
    // that changed since, or is another ISO's, is never overwritten without asking (audit A5).
    const std::filesystem::path iso = source->path;
    const std::filesystem::path folder = m_state.settings().workDirectoryFor(iso);
    const core::WorkCopyState copy = core::inspectWorkCopy(iso, folder);
    log::info("app", std::format(L"work copy {}: {}", folder.wstring(), core::workCopyStateName(copy)));
    std::weak_ptr<bool> alive = m_alive; // the answer to a dialog may come after a UI rebuild
    auto prepare = [this, alive, iso, folder, index, purpose, rows = std::move(rows), next = std::move(next)](bool extract,
                                                                                                         bool fresh) mutable {
        if (const auto a = alive.lock(); !a || !*a) {
            return;
        }
        prepareWorkCopy(iso, folder, index, extract, fresh, std::move(next), purpose, rows);
    };
    switch (copy) {
    case core::WorkCopyState::Missing:
    case core::WorkCopyState::Partial: prepare(/*extract=*/true, /*fresh=*/false); return;
    case core::WorkCopyState::Pristine: prepare(/*extract=*/false, /*fresh=*/false); return;
    case core::WorkCopyState::Modified:
    case core::WorkCopyState::Unknown:
    case core::WorkCopyState::OtherSource: break;
    }
    if (!m_events.workCopyChoice) {
        if (copy == core::WorkCopyState::OtherSource) {
            m_events.refused(Str::ImagesWorkCopyOther);
            return;
        }
        prepare(/*extract=*/false, /*fresh=*/false);
        return;
    }
    m_events.workCopyChoice(copy, folder, [prepare = std::move(prepare)](bool fresh) mutable {
        prepare(/*extract=*/fresh, fresh);
    });
}

void ImageController::prepareWorkCopy(const std::filesystem::path& iso, const std::filesystem::path& folder, int index,
                                      bool extract, bool fresh, std::function<void()> next, EngineOperation::Kind purpose,
                                      const std::vector<int>& rows) {
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    auto opened = std::make_shared<std::optional<core::SourceInfo>>();
    // The rows the work after it is about say what that work is (a delete's copy step: "Siliniyor").
    EngineOperation op{EngineOperation::Kind::Preparing, iso.filename().wstring(), folder, rows.size() == 1 ? rows.front() : 0};
    op.indexes = rows.size() > 1 ? rows : std::vector<int>{};
    op.purpose = purpose;
    run(std::move(op),
        [iso, folder, opened, extract, fresh](const core::TaskContext& task) -> Result<void> {
            if (extract) {
                if (auto r = core::extractWorkCopy(iso, folder, fresh, task); !r) {
                    return r;
                }
            }
            auto info = core::openSource(folder);
            if (!info) {
                return std::unexpected(info.error());
            }
            *opened = std::move(*info);
            return {};
        },
        [this, opened, index, next = std::move(next)] {
            m_state.setSource(std::move(**opened));
            m_state.select(index);
            next();
        },
        Failure::Prepare);
}

void ImageController::mount(int index) {
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    if (m_state.mounted()) {
        m_events.refused(Str::ImagesUnmountFirst);
        return;
    }
    if (isPackedSource()) {
        m_events.refused(Str::ImagesEsdNoMount);
        return;
    }
    if (!core::isElevated()) {
        // Relaunch through UAC and continue: reopen this source and mount the same index.
        m_events.needsAdmin(std::format(L"{} --page=images --mount={}", core::quoteArgument(m_state.source()->path.wstring()), index));
        return;
    }
    if (isVirtualDiskSource()) {
        // D-141: what happens to the user's disk is said first (changes disk, save = merge).
        const std::filesystem::path vhd = m_state.source()->path;
        const std::filesystem::path child = core::changesDiskPath(vhd, m_state.settings().vhdWorkDirectory());
        if (!m_events.confirmVirtualDisk) {
            mountVirtualDisk();
            return;
        }
        std::weak_ptr<bool> alive = m_alive;
        m_events.confirmVirtualDisk(vhd, child, [this, alive] {
            if (const auto a = alive.lock(); a && *a) {
                mountVirtualDisk();
            }
        });
        return;
    }
    withWritableSource(index, EngineOperation::Kind::Mounting, {index}, [this, index] {
        const auto wim = installWimPath();
        const auto mountDir = m_state.settings().mountDirectory();
        const std::wstring edition = editionName(index);
        run(EngineOperation{EngineOperation::Kind::Mounting, edition, mountDir, index},
            [wim = *wim, mountDir, index](const core::TaskContext& task) -> Result<void> {
                auto dism = core::Dism::instance();
                if (!dism) {
                    return std::unexpected(dism.error());
                }
                // Leftovers, stale folder state and "folder busy" retries: see mountSafely.
                auto outcome = core::mountSafely(**dism, wim, index, mountDir, /*readOnly=*/false, task);
                if (!outcome) {
                    return std::unexpected(outcome.error());
                }
                return {};
            },
            [this, wim = *wim, mountDir, index, edition] {
                m_state.setMounted(MountedImage{mountDir, wim, index, edition, false});
                m_events.succeeded(Str::ImagesMountedToast, edition);
                if (m_events.mounted) {
                    m_events.mounted();
                }
            },
            Failure::Mount);
    });
}

void ImageController::mountVirtualDisk() {
    if (busy() || m_state.mounted() || !isVirtualDiskSource()) {
        return;
    }
    const std::filesystem::path vhd = m_state.source()->path;
    const std::filesystem::path folder = m_state.settings().vhdMountDirectory();
    const std::filesystem::path work = m_state.settings().vhdWorkDirectory();
    const std::wstring edition = editionName(1);
    auto record = std::make_shared<std::optional<core::VhdMountRecord>>();
    run(EngineOperation{EngineOperation::Kind::Mounting, edition, folder, 1},
        [vhd, folder, work, record](const core::TaskContext& task) -> Result<void> {
            auto mounted = core::mountVirtualDisk(vhd, folder, work, L"WinLove", task);
            if (!mounted) {
                return std::unexpected(mounted.error());
            }
            *record = std::move(*mounted);
            return {};
        },
        [this, record, vhd, edition] {
            m_state.setMounted(MountedImage{(*record)->folder, vhd, 1, edition, false});
            m_events.succeeded(Str::ImagesMountedToast, edition);
            if (m_events.mounted) {
                m_events.mounted();
            }
        },
        Failure::Mount);
}

void ImageController::unmountVirtualDisk(bool commit) {
    const auto mounted = m_state.mounted();
    if (!mounted || busy()) {
        return;
    }
    auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
    m_state.beginRunClose(commit); // a finished run's image: its card says so (2026-11-11)
    run(EngineOperation{EngineOperation::Kind::Unmounting, mounted->edition, mounted->mountDir, mounted->index},
        [dir = mounted->mountDir, vhd = mounted->imagePath, commit, reopened](const core::TaskContext& task) -> Result<void> {
            auto outcome = core::unmountVirtualDisk(dir, commit, task);
            if (!outcome) {
                return std::unexpected(outcome.error());
            }
            if (commit) {
                // What the saved disk holds now (its size): read again, read only, detached at once.
                if (auto info = core::openSource(vhd)) {
                    *reopened = std::move(*info);
                } else {
                    log::warn("images", L"saved, but the disk was not read again: " + describe(info.error()));
                }
            }
            return {};
        },
        [this, reopened, commit] {
            if (*reopened) {
                noteSavedSize(**reopened, 1);
            }
            m_state.endRunClose(std::nullopt);
            m_state.setMounted(std::nullopt);
            if (*reopened && m_state.source() && core::isVirtualDiskFormat(m_state.source()->format)) {
                m_state.setSource(std::move(**reopened));
                m_state.select(1);
            }
            m_events.succeeded(commit ? Str::ImagesVhdSavedToast : Str::ImagesVhdDiscardedToast, L"");
        },
        Failure::Unmount, [this](const Error& e) { m_state.endRunClose(e, e.code == ErrorCode::Cancelled); });
}

void ImageController::noteSavedSize(const core::SourceInfo& source, int index) {
    auto& run = m_state.applyRunMutable();
    if (!run || !run->closing || !run->closing->commit) {
        return;
    }
    for (const auto& image : source.install.images) {
        if (image.index == index) {
            run->sizeAfter = image.totalBytes; // what the save came to (the run's card: before → after)
        }
    }
}

void ImageController::unmount(bool commit) {
    const auto mounted = m_state.mounted();
    if (!mounted || busy()) {
        return;
    }
    if (core::isVirtualDiskPath(mounted->imagePath)) {
        unmountVirtualDisk(commit); // D-141: detach; save = merge the changes disk, discard = delete it
        return;
    }
    auto recovered = std::make_shared<bool>(false);
    auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
    // 2026-11-11: an image taken over from another tool — DISM's commit of that folder only; its WIM
    // is not rewritten (replaced) under the tool that keeps it.
    const bool elsewhere = m_state.mountedElsewhere();
    // Saving what a finished run left mounted ends that run: its card follows the save / discard, and
    // a save reads the edition's size again for it.
    const bool closesRun = m_state.runLeftImageMounted();
    const std::filesystem::path sourcePath = closesRun && commit && m_state.source() ? m_state.source()->path : std::filesystem::path();
    m_state.beginRunClose(commit);
    run(EngineOperation{EngineOperation::Kind::Unmounting, mounted->edition, mounted->mountDir, mounted->index},
        [dir = mounted->mountDir, wim = elsewhere ? std::filesystem::path() : mounted->imagePath, commit, recovered, sourcePath,
         reopened](const core::TaskContext& task) -> Result<void> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            // Hives, Explorer windows and partial unmounts are handled in unmountSafely.
            auto outcome = core::unmountSafely(**dism, dir, commit, task);
            if (!outcome) {
                return std::unexpected(outcome.error());
            }
            *recovered = outcome->recovered;
            // A save keeps what the commit orphaned in the WIM (the removed files' streams): rewrite
            // it without them, as Apply does after its own commit. Never fatal: the image is saved.
            if (commit && !wim.empty()) {
                if (auto rewritten = core::optimizeWim(wim, core::TaskContext{}); !rewritten) {
                    log::warn("images", L"image saved, but not rewritten without the commit's leftovers: " +
                                            describe(rewritten.error()));
                }
            }
            if (!sourcePath.empty()) {
                if (auto info = core::openSource(sourcePath)) {
                    *reopened = std::move(*info);
                } else {
                    log::warn("images", L"saved, but the source was not read again: " + describe(info.error()));
                }
            }
            return {};
        },
        [this, recovered, reopened, index = mounted->index] {
            if (*reopened) {
                noteSavedSize(**reopened, index);
            }
            m_state.endRunClose(std::nullopt);
            m_state.setMounted(std::nullopt);
            if (*reopened && m_state.source() && m_state.source()->path == (*reopened)->path) {
                const auto selected = m_state.selectedIndex();
                m_state.setSource(std::move(**reopened)); // the saved edition's new size and date
                m_state.select(selected);
            }
            m_events.succeeded(*recovered ? Str::ImagesUnmountRecovered : Str::ImagesUnmountedToast, L"");
            inspectMountFolder();
        },
        Failure::Unmount, [this](const Error& e) { m_state.endRunClose(e, e.code == ErrorCode::Cancelled); });
}

void ImageController::scanSystemMounts() {
    if (!core::isElevated()) {
        return;
    }
    if (const auto& current = m_state.systemMounts(); !current || current->status != AppState::SystemMounts::Status::Loading) {
        AppState::SystemMounts loading;
        if (current) {
            loading.items = current->items; // the old list stays on screen while it is read again
        }
        m_state.setSystemMounts(std::move(loading));
    }
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // 2026-11-11 (the owner: other tools' mounts showed up after half a minute): wimgapi's list on the
    // reader thread first — no DISM session, nothing queued before it — shown at once; DISM's own
    // reading below (behind whatever the engine is doing) replaces it when it arrives.
    m_state.reader().run<std::vector<core::MountCheck>>(
        [](const core::TaskContext&) { return core::inspectMountsFast(); },
        [this, post, alive](Result<std::vector<core::MountCheck>> result) {
            if (!result) {
                log::warn("app", describe(result.error())); // DISM's reading still comes
                return;
            }
            post([this, alive, items = std::move(*result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& current = m_state.systemMounts();
                if (current && current->status != AppState::SystemMounts::Status::Loading) {
                    return; // DISM's reading came first
                }
                AppState::SystemMounts early; // still Loading: DISM's reading follows
                early.items = std::move(items);
                m_state.setSystemMounts(std::move(early));
            });
        },
        {});
    m_state.engine().run<std::vector<core::MountCheck>>(
        [](const core::TaskContext&) -> Result<std::vector<core::MountCheck>> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            return core::inspectMounts(**dism);
        },
        [this, post, alive](Result<std::vector<core::MountCheck>> result) {
            post([this, alive, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                AppState::SystemMounts mounts;
                if (!result) {
                    log::warn("app", describe(result.error()));
                    mounts.status = AppState::SystemMounts::Status::Failed;
                    mounts.error = result.error();
                } else {
                    mounts.status = AppState::SystemMounts::Status::Ready;
                    mounts.items = std::move(*result);
                    log::info("app", std::format(L"{} image(s) mounted on this PC", mounts.items.size()));
                }
                m_state.setSystemMounts(std::move(mounts));
            });
        },
        {});
}

void ImageController::adoptMount(const std::filesystem::path& folderInput) {
    const std::filesystem::path folder = withoutLongPathPrefix(nativePath(folderInput));
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    if (const auto& mounted = m_state.mounted()) {
        if (_wcsicmp(withoutLongPathPrefix(nativePath(mounted->mountDir)).c_str(), folder.c_str()) != 0) {
            m_events.refused(Str::SourceMountedBusy);
        }
        return; // the same mount: already the one WinLove works on
    }
    if (!core::isElevated()) {
        m_events.needsAdmin(L"--page=source"); // DISM's record of the mount needs it
        return;
    }
    // 2026-11-11 (the owner: another tool's mount stayed at "Bağlanıyor %0", then "folder has no
    // sources\install.wim"): one engine job does it all — DISM's record of the folder, a remount
    // when DISM asks for one (said as such), the source its image opens as (the image file itself
    // unless it really is setup media) — and every failure ends the work and says why.
    m_adoptState.reset();
    auto found = std::make_shared<std::optional<core::MountState>>();
    auto record = std::make_shared<std::optional<core::MountInfo>>();
    auto opened = std::make_shared<std::optional<core::SourceInfo>>();
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    EngineOperation op{EngineOperation::Kind::Adopting, folder.filename().wstring(), folder, 0};
    run(std::move(op),
        [this, folder, found, record, opened, post, alive](const core::TaskContext& task) -> Result<void> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto check = core::inspectMount(**dism, folder);
            if (!check) {
                return std::unexpected(check.error());
            }
            *found = check->state;
            if (core::adoptionStep(check->state) == core::AdoptStep::Remount && post) {
                post([this, alive] { // the strip and the status bar say "Yeniden bağlanıyor"
                    if (const auto a = alive.lock(); a && *a && m_state.operation() &&
                                                     m_state.operation()->kind == EngineOperation::Kind::Adopting) {
                        auto now = *m_state.operation();
                        now.kind = EngineOperation::Kind::Remounting;
                        m_state.beginOperation(std::move(now));
                    }
                });
            }
            auto adopted = core::adoptMount(**dism, *check, task);
            if (!adopted) {
                return std::unexpected(adopted.error());
            }
            *record = adopted->record;
            auto source = core::openMountedImageSource((*record)->imagePath);
            if (!source) {
                return std::unexpected(source.error());
            }
            *opened = std::move(*source);
            return {};
        },
        [this, record, opened] {
            if (!*record || !*opened) {
                return;
            }
            const auto& m = **record;
            MountedImage mounted{withoutLongPathPrefix(m.mountPath), withoutLongPathPrefix(m.imagePath), m.index, {}, m.readOnly};
            mounted.edition = std::format(L"#{}", m.index);
            for (const auto& image : (*opened)->install.images) {
                if (image.index == m.index) {
                    mounted.edition = image.name;
                }
            }
            log::info("app", std::format(L"taking over the mount {} <- {} [{}] (source {})", mounted.mountDir.wstring(),
                                         mounted.imagePath.wstring(), m.index, (*opened)->path.wstring()));
            if (m_events.adopted) {
                m_events.adopted(std::move(**opened), std::move(mounted));
                return;
            }
            const int index = mounted.index;
            m_state.setSource(std::move(**opened));
            m_state.select(index);
            m_state.setMounted(std::move(mounted));
        },
        Failure::Adopt, [this, found](const Error&) { m_adoptState = *found; });
}

void ImageController::discardMount(const std::filesystem::path& folder, std::wstring edition) {
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    if (const auto& mounted = m_state.mounted();
        mounted && _wcsicmp(nativePath(mounted->mountDir).c_str(), nativePath(folder).c_str()) == 0) {
        unmount(/*commit=*/false); // the one WinLove works on: its own path
        return;
    }
    run(EngineOperation{EngineOperation::Kind::Unmounting, std::move(edition), folder, 0},
        [folder](const core::TaskContext& task) -> Result<void> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto check = core::inspectMount(**dism, folder);
            if (!check) {
                return std::unexpected(check.error());
            }
            if (check->state == core::MountState::Ok || check->state == core::MountState::NeedsRemount) {
                auto outcome = core::unmountSafely(**dism, folder, /*commit=*/false, task);
                return outcome ? Result<void>{} : std::unexpected(outcome.error());
            }
            // Invalid / image missing / leftovers: what repairMount does about them (discard, clean up).
            auto repaired = core::repairMount(**dism, *check, task);
            return repaired ? Result<void>{} : std::unexpected(repaired.error());
        },
        [this, folder] {
            m_events.succeeded(Str::ImagesUnmountedToast, folder.wstring());
            scanSystemMounts();
            inspectMountFolder();
        },
        Failure::Unmount);
}

void ImageController::exportIndex(int index, const std::filesystem::path& destination) {
    if (busy() || isVirtualDiskSource()) {
        m_events.refused(busy() ? Str::ImagesBusy : Str::ImagesVhdNoWim);
        return;
    }
    withWritableSource(index, EngineOperation::Kind::Exporting, {index}, [this, index, destination] {
        const auto wim = installWimPath();
        run(EngineOperation{EngineOperation::Kind::Exporting, editionName(index), destination, index},
            [wim = *wim, index, destination](const core::TaskContext& task) -> Result<void> {
                return core::exportImage(wim, index, destination, core::WimCompression::Lzx, task);
            },
            [this, destination] { m_events.succeeded(Str::ImagesExportedToast, destination.filename().wstring()); },
            Failure::Export);
    });
}

void ImageController::exportEditions(std::vector<int> indexes, const std::filesystem::path& destination) {
    if (busy() || isVirtualDiskSource()) {
        m_events.refused(busy() ? Str::ImagesBusy : Str::ImagesVhdNoWim);
        return;
    }
    if (indexes.empty()) {
        return;
    }
    const int selected = m_state.selectedIndex().value_or(indexes.front());
    const std::vector<int> rows = indexes;
    withWritableSource(selected, EngineOperation::Kind::Exporting, rows, [this, indexes = std::move(indexes), destination] {
        const auto wim = installWimPath();
        EngineOperation op{EngineOperation::Kind::Exporting, destination.filename().wstring(), destination, 0};
        op.indexes = indexes; // each marked edition's row says so
        run(std::move(op),
            [wim = *wim, indexes, destination](const core::TaskContext& task) -> Result<void> {
                // One after another into the same file: progress across all of them.
                for (std::size_t i = 0; i < indexes.size(); ++i) {
                    const core::TaskContext part{task.cancel, [&](double f, std::wstring_view s) {
                                                     task.report((static_cast<double>(i) + f) / static_cast<double>(indexes.size()), s);
                                                 }};
                    if (auto r = core::exportImage(wim, indexes[i], destination, core::WimCompression::Lzx, part); !r) {
                        return r;
                    }
                }
                return {};
            },
            [this, destination] { m_events.succeeded(Str::ImagesExportedToast, destination.filename().wstring()); },
            Failure::Export);
    });
}

void ImageController::renameEdition(int index, std::wstring name, std::wstring description) {
    if (const auto refusal = editRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    withWritableSource(index, EngineOperation::Kind::Renaming, {index}, [this, index, name = std::move(name), description = std::move(description)] {
        const auto wim = installWimPath();
        const std::filesystem::path sourcePath = m_state.source()->path;
        auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
        run(EngineOperation{EngineOperation::Kind::Renaming, editionName(index), *wim, index},
            [wim = *wim, index, name, description, sourcePath, reopened](const core::TaskContext&) -> Result<void> {
                if (auto r = core::setImageText(wim, index, core::ImageText{name, description, std::nullopt}); !r) {
                    return r;
                }
                auto info = core::openSource(sourcePath);
                if (!info) {
                    return std::unexpected(info.error());
                }
                *reopened = std::move(*info);
                return {};
            },
            [this, reopened, index] {
                // Same editions under the same indexes: what was marked stays marked.
                const std::vector<int> marked = m_state.selection();
                m_state.setSource(std::move(**reopened));
                m_state.selectMany(marked, index);
                m_events.succeeded(Str::ImagesRenamedToast, editionName(index));
            },
            Failure::Rename);
    });
}

void ImageController::readEditions(std::function<void(const core::ImageEditions&)> done) {
    const auto mounted = m_state.mounted();
    if (!mounted) {
        m_events.refused(Str::ApplyNoMountTitle);
        return;
    }
    if (m_editions && m_editions->mountDir == mounted->mountDir && m_editions->imagePath == mounted->imagePath &&
        m_editions->index == mounted->index) {
        done(m_editions->editions);
        return;
    }
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    auto read = std::make_shared<std::optional<core::ImageEditions>>();
    run(EngineOperation{EngineOperation::Kind::Editions, mounted->edition, mounted->mountDir, mounted->index},
        [mountDir = mounted->mountDir, read](const core::TaskContext&) -> Result<void> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto session = (*dism)->openSession(mountDir);
            if (!session) {
                return std::unexpected(session.error());
            }
            auto editions = core::readEditions(**session);
            if (!editions) {
                return std::unexpected(editions.error());
            }
            *read = std::move(*editions);
            return {};
        },
        [this, read, mounted = *mounted, done = std::move(done)] {
            m_editions = KnownEditions{mounted.mountDir, mounted.imagePath, mounted.index, **read};
            done(**read);
        },
        Failure::Editions);
}

void ImageController::checkHealth(bool scan) {
    const auto mounted = m_state.mounted();
    if (!mounted) {
        m_events.refused(Str::ApplyNoMountTitle);
        return;
    }
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    auto report = std::make_shared<std::optional<core::ImageHealthReport>>();
    run(EngineOperation{EngineOperation::Kind::Health, mounted->edition, mounted->mountDir, mounted->index},
        [mountDir = mounted->mountDir, scan, report](const core::TaskContext& task) -> Result<void> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto session = (*dism)->openSession(mountDir);
            if (!session) {
                return std::unexpected(session.error());
            }
            auto r = core::checkImageHealth(**session, scan, task);
            if (!r) {
                return std::unexpected(r.error());
            }
            *report = std::move(*r);
            return {};
        },
        [this, report] {
            if (m_events.checkedHealth && *report) {
                m_events.checkedHealth(**report);
            }
        },
        Failure::Health);
}

void ImageController::repairHealth(std::wstring source, bool limitAccess) {
    const auto mounted = m_state.mounted();
    if (!mounted) {
        m_events.refused(Str::ApplyNoMountTitle);
        return;
    }
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    auto report = std::make_shared<std::optional<core::ImageHealthReport>>();
    run(EngineOperation{EngineOperation::Kind::Health, mounted->edition, mounted->mountDir, mounted->index},
        [mountDir = mounted->mountDir, source = std::move(source), limitAccess, report](
            const core::TaskContext& task) -> Result<void> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto session = (*dism)->openSession(mountDir);
            if (!session) {
                return std::unexpected(session.error());
            }
            auto r = core::restoreImageHealth(**session, source, limitAccess, task);
            if (!r) {
                return std::unexpected(r.error());
            }
            *report = std::move(*r);
            return {};
        },
        [this, report] {
            if (m_events.checkedHealth && *report) {
                m_events.checkedHealth(**report);
            }
        },
        Failure::Health);
}

void ImageController::rememberEditions(core::ImageEditions editions) {
    if (const auto& mounted = m_state.mounted()) {
        m_editions = KnownEditions{mounted->mountDir, mounted->imagePath, mounted->index, std::move(editions)};
    }
}

void ImageController::verify() {
    if (const auto refusal = verifyRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    const core::SourceInfo source = *m_state.source();
    const std::wstring file = std::filesystem::path(source.installImage).filename().wstring();
    auto report = std::make_shared<std::optional<core::WimVerifyReport>>();
    run(EngineOperation{EngineOperation::Kind::Verifying, file, source.path, 0},
        [source, report](const core::TaskContext& task) -> Result<void> {
            auto bytes = core::openInstallImage(source);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            auto verified = core::verifyWim(**bytes, task);
            if (!verified) {
                return std::unexpected(verified.error());
            }
            *report = std::move(*verified);
            return {};
        },
        [this, report, file] {
            if (m_events.verified) {
                m_events.verified(**report, file);
            }
        },
        Failure::Verify);
}

void ImageController::convertEsd(const std::filesystem::path& destination) {
    if (busy() || !isPackedSource()) {
        m_events.refused(busy() ? Str::ImagesBusy : Str::ImagesEsdOnly);
        return;
    }
    withWritableSource(m_state.selectedIndex().value_or(1), EngineOperation::Kind::Exporting, {}, [this, destination] {
        const auto esd = installWimPath();
        std::vector<int> indexes;
        for (const auto& image : m_state.source()->install.images) {
            indexes.push_back(image.index);
        }
        run(EngineOperation{EngineOperation::Kind::Exporting, esd->filename().wstring(), destination, 0},
            [esd = *esd, indexes, destination](const core::TaskContext& task) -> Result<void> {
                // Every edition, one after another, into one WIM (LZX): progress across all of them.
                for (std::size_t i = 0; i < indexes.size(); ++i) {
                    const core::TaskContext part{task.cancel, [&](double f, std::wstring_view s) {
                                                     task.report((static_cast<double>(i) + f) / static_cast<double>(indexes.size()), s);
                                                 }};
                    if (auto r = core::exportImage(esd, indexes[i], destination, core::WimCompression::Lzx, part); !r) {
                        return r;
                    }
                }
                return {};
            },
            [this, destination] { m_events.succeeded(Str::ImagesExportedToast, destination.filename().wstring()); },
            Failure::Export);
    });
}

// ---- D-058 tools ----------------------------------------------------------------------------

bool ImageController::isSwmSource() const {
    return m_state.source() && m_state.source()->install.header.totalParts > 1;
}

std::optional<Str> ImageController::toolsRefusal() const {
    if (!m_state.source()) {
        return Str::ImagesEmptyTitle;
    }
    if (isVirtualDiskSource()) {
        return Str::ImagesVhdNoWim;
    }
    if (busy()) {
        return Str::ImagesBusy;
    }
    if (m_state.mounted()) {
        return Str::ImagesUnmountFirst;
    }
    return std::nullopt;
}

void ImageController::recompress(core::WimCompression target) {
    if (const auto refusal = toolsRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    if (isSwmSource()) {
        m_events.refused(Str::ImagesSwmJoinFirst);
        return;
    }
    withWritableSource(m_state.selectedIndex().value_or(1), EngineOperation::Kind::Exporting, {}, [this, target] {
        const auto wim = installWimPath();
        const auto source = *m_state.source();
        auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
        run(EngineOperation{EngineOperation::Kind::Exporting, wim->filename().wstring(), *wim, 0},
            [wim = *wim, target, source, reopened](const core::TaskContext& task) -> Result<void> {
                auto result = core::recompressWim(wim, target, task);
                if (!result) {
                    return std::unexpected(result.error());
                }
                // A file source is the file (maybe renamed .wim <-> .esd); a setup folder stays the folder.
                const std::filesystem::path reopen = source.format == core::ImageFormat::Folder ? source.path : *result;
                auto info = core::openSource(reopen);
                if (!info) {
                    return std::unexpected(info.error());
                }
                *reopened = std::move(*info);
                return {};
            },
            [this, reopened] {
                const std::wstring name = std::filesystem::path((*reopened)->installImage).filename().wstring();
                m_state.setSource(std::move(**reopened));
                m_events.succeeded(Str::ImagesRecompressedToast, name);
            },
            Failure::Export);
    });
}

void ImageController::splitSwm(const std::filesystem::path& firstPart, std::uint64_t partMiB) {
    if (const auto refusal = toolsRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    if (isEsdSource() || isSwmSource()) {
        m_events.refused(isEsdSource() ? Str::ImagesSplitNeedsWim : Str::ImagesSwmJoinFirst);
        return;
    }
    withWritableSource(m_state.selectedIndex().value_or(1), EngineOperation::Kind::Exporting, {}, [this, firstPart, partMiB] {
        const auto wim = installWimPath();
        auto parts = std::make_shared<int>(0);
        run(EngineOperation{EngineOperation::Kind::Exporting, wim->filename().wstring(), firstPart, 0},
            [wim = *wim, firstPart, partMiB, parts](const core::TaskContext& task) -> Result<void> {
                std::error_code ec;
                std::filesystem::create_directories(firstPart.parent_path(), ec);
                auto split = core::splitWim(wim, firstPart, partMiB << 20, task);
                if (!split) {
                    return std::unexpected(split.error());
                }
                *parts = *split;
                return {};
            },
            [this, parts, firstPart] {
                m_events.succeeded(Str::ImagesSplitToast,
                                   std::format(L"{} · {}", firstPart.filename().wstring(), *parts));
            },
            Failure::Export);
    });
}

void ImageController::mergeSwm(const std::filesystem::path& destination) {
    if (const auto refusal = toolsRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    if (!isSwmSource() || m_state.source()->format == core::ImageFormat::Iso) {
        m_events.refused(Str::ImagesSwmOnly);
        return;
    }
    const auto first = installWimPath();
    auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
    run(EngineOperation{EngineOperation::Kind::Exporting, first->filename().wstring(), destination, 0},
        [first = *first, destination, reopened](const core::TaskContext& task) -> Result<void> {
            if (auto r = core::mergeSplitWim(first, destination, task); !r) {
                return r;
            }
            auto info = core::openSource(destination);
            if (!info) {
                return std::unexpected(info.error());
            }
            *reopened = std::move(*info);
            return {};
        },
        [this, reopened, destination] {
            m_state.setSource(std::move(**reopened));
            m_events.succeeded(Str::ImagesExportedToast, destination.filename().wstring());
        },
        Failure::Export);
}

void ImageController::duplicateEdition(int index, std::wstring name) {
    if (const auto refusal = editRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    withWritableSource(index, EngineOperation::Kind::Duplicating, {index}, [this, index, name = std::move(name)] {
        const auto wim = installWimPath();
        const std::filesystem::path sourcePath = m_state.source()->path;
        auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
        auto added = std::make_shared<int>(0);
        run(EngineOperation{EngineOperation::Kind::Duplicating, editionName(index), *wim, index},
            [wim = *wim, index, name, sourcePath, reopened, added](const core::TaskContext& task) -> Result<void> {
                auto copy = core::duplicateEdition(wim, index, name, task);
                if (!copy) {
                    return std::unexpected(copy.error());
                }
                *added = *copy;
                auto info = core::openSource(sourcePath);
                if (!info) {
                    return std::unexpected(info.error());
                }
                *reopened = std::move(*info);
                return {};
            },
            [this, reopened, added, name] {
                m_state.setSource(std::move(**reopened));
                m_state.selectMany({*added}, *added);
                m_events.succeeded(Str::ImagesDuplicatedToast, name);
            },
            Failure::Export);
    });
}

void ImageController::appendFrom(const std::filesystem::path& other, std::vector<int> indexes) {
    if (const auto refusal = editRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    if (indexes.empty()) {
        return;
    }
    withWritableSource(m_state.selectedIndex().value_or(1), EngineOperation::Kind::Exporting, {}, [this, other, indexes = std::move(indexes)] {
        const auto wim = installWimPath();
        const std::filesystem::path sourcePath = m_state.source()->path;
        const core::WimCompression compression = m_state.source()->install.header.compression;
        const std::filesystem::path scratch = m_state.settings().workDirectoryFor(sourcePath) / L"append";
        auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
        const std::size_t count = indexes.size();
        const int firstNew = static_cast<int>(m_state.source()->install.images.size()) + 1;
        run(EngineOperation{EngineOperation::Kind::Exporting, other.filename().wstring(), *wim, 0},
            [wim = *wim, other, indexes, compression, scratch, sourcePath, reopened, firstNew](const core::TaskContext& task) -> Result<void> {
                auto from = core::openSource(other);
                if (!from) {
                    return std::unexpected(from.error());
                }
                bool extracted = false;
                const core::TaskContext extract{task.cancel, [&](double f, std::wstring_view s) { task.report(f * 0.3, s); }};
                auto file = core::installImageFile(*from, scratch, extracted, extract);
                if (!file) {
                    return std::unexpected(file.error());
                }
                const core::TaskContext add{task.cancel, [&](double f, std::wstring_view s) {
                                                task.report((extracted ? 0.3 : 0.0) + f * (extracted ? 0.7 : 1.0), s);
                                            }};
                auto added = core::exportImages(*file, indexes, wim, compression, add);
                std::error_code ec;
                if (extracted) {
                    std::filesystem::remove_all(scratch, ec);
                }
                if (!added) {
                    return added;
                }
                // D-077: an added edition named like one already there gets its release in brackets.
                if (const auto now = core::openSource(sourcePath)) {
                    const auto& images = now->install.images;
                    for (const auto& [index, name] : core::distinctEditionNames(images, firstNew)) {
                        const auto image = std::ranges::find(images, index, &core::ImageInfo::index);
                        if (auto r = core::setImageText(wim, index, {name, image->description, std::nullopt}); !r) {
                            log::warn("images", describe(r.error()));
                        }
                    }
                }
                auto info = core::openSource(sourcePath);
                if (!info) {
                    return std::unexpected(info.error());
                }
                *reopened = std::move(*info);
                return {};
            },
            [this, reopened, count] {
                m_state.setSource(std::move(**reopened));
                m_events.succeeded(Str::ImagesAppendedToast, std::to_wstring(count));
            },
            Failure::Export);
    });
}

void ImageController::replaceSetupMedia(const std::filesystem::path& from) {
    if (const auto refusal = editRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    withWritableSource(m_state.selectedIndex().value_or(1), EngineOperation::Kind::Preparing, {}, [this, from] {
        const std::filesystem::path folder = m_state.source()->path;
        auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
        run(EngineOperation{EngineOperation::Kind::Preparing, from.filename().wstring(), folder, 0},
            [folder, from, reopened](const core::TaskContext& task) -> Result<void> {
                if (auto r = core::replaceSetupMedia(folder, from, task); !r) {
                    return r;
                }
                auto info = core::openSource(folder);
                if (!info) {
                    return std::unexpected(info.error());
                }
                *reopened = std::move(*info);
                return {};
            },
            [this, reopened] {
                m_state.setSource(std::move(**reopened));
                m_events.succeeded(Str::IsoAioReplaced, L"");
            },
            Failure::Export);
    });
}

void ImageController::capture(const std::filesystem::path& folder, const std::filesystem::path& wim, core::ImageText text,
                              core::WimCompression compression) {
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    if (!core::isElevated()) {
        m_events.needsAdmin(L"--page=source");
        return;
    }
    auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
    run(EngineOperation{EngineOperation::Kind::Exporting, folder.filename().wstring(), wim, 0},
        [folder, wim, text = std::move(text), compression, reopened](const core::TaskContext& task) -> Result<void> {
            auto added = core::captureImage(folder, wim, text, compression, task);
            if (!added) {
                return std::unexpected(added.error());
            }
            auto info = core::openSource(wim);
            if (!info) {
                return std::unexpected(info.error());
            }
            *reopened = std::move(*info);
            return {};
        },
        [this, reopened, wim] {
            m_state.setSource(std::move(**reopened));
            m_events.succeeded(Str::ImagesCapturedToast, wim.filename().wstring());
        },
        Failure::Export);
}

void ImageController::moveEdition(int index, int delta) {
    if (const auto refusal = deleteRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    const int count = static_cast<int>(m_state.source()->install.images.size());
    const int target = index + delta;
    if (index < 1 || index > count || target < 1 || target > count || target == index) {
        return;
    }
    // The two editions trade places; the rest stay where they are.
    std::vector<int> order(static_cast<std::size_t>(count));
    std::iota(order.begin(), order.end(), 1);
    std::swap(order[static_cast<std::size_t>(index - 1)], order[static_cast<std::size_t>(target - 1)]);
    const std::wstring name = editionName(index);
    withWritableSource(index, EngineOperation::Kind::Moving, {index}, [this, order, index, target, name] {
        const auto wim = installWimPath();
        const std::filesystem::path sourcePath = m_state.source()->path;
        auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
        run(EngineOperation{EngineOperation::Kind::Moving, name, *wim, index},
            [wim = *wim, order, sourcePath, reopened](const core::TaskContext& task) -> Result<void> {
                if (auto r = core::reorderImages(wim, order, task); !r) {
                    return r;
                }
                auto info = core::openSource(sourcePath);
                if (!info) {
                    return std::unexpected(info.error());
                }
                *reopened = std::move(*info);
                return {};
            },
            [this, reopened, index, target, name] {
                m_state.setSource(std::move(**reopened));
                m_state.selectMany({target}, target);
                // The answer file's edition follows its edition.
                if (auto unattend = m_state.unattend(); unattend.options.imageIndex == index || unattend.options.imageIndex == target) {
                    unattend.options.imageIndex = unattend.options.imageIndex == index ? target : index;
                    m_state.setUnattend(std::move(unattend));
                }
                m_events.succeeded(Str::ImagesMovedToast, name);
            },
            Failure::Export);
    });
}

void ImageController::removeEditions(std::vector<int> indexes, std::wstring label) {
    if (const auto refusal = deleteRefusal()) {
        m_events.refused(*refusal);
        return;
    }
    if (indexes.empty()) {
        return;
    }
    if (indexes.size() >= m_state.source()->install.images.size()) {
        m_events.refused(Str::ImagesLastEdition);
        return;
    }
    // One edition: its row shows the work. Several: no single row does.
    const int row = indexes.size() == 1 ? indexes.front() : 0;
    const int selected = m_state.selectedIndex().value_or(indexes.front());
    const std::vector<int> rows = indexes;
    withWritableSource(selected, EngineOperation::Kind::Deleting, rows, [this, indexes = std::move(indexes), label = std::move(label), row] {
        const auto wim = installWimPath();
        const std::filesystem::path sourcePath = m_state.source()->path;
        auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
        EngineOperation op{EngineOperation::Kind::Deleting, label, *wim, row};
        op.indexes = indexes.size() > 1 ? indexes : std::vector<int>{}; // several: each of their rows
        run(std::move(op),
            [wim = *wim, indexes, sourcePath, reopened](const core::TaskContext& task) -> Result<void> {
                if (auto r = core::removeImages(wim, indexes, task); !r) {
                    return r;
                }
                auto info = core::openSource(sourcePath);
                if (!info) {
                    return std::unexpected(info.error());
                }
                *reopened = std::move(*info);
                return {};
            },
            [this, reopened, indexes, label] {
                // What stays is renumbered: the selection and the answer file's edition follow it.
                const auto selected = m_state.selectedIndex();
                const std::vector<int> marked = m_state.selection();
                m_state.setSource(std::move(**reopened));
                std::vector<int> kept;
                for (const int index : marked) {
                    if (const auto now = core::indexAfterRemoval(index, indexes)) {
                        kept.push_back(*now);
                    }
                }
                if (!kept.empty()) {
                    const auto primary = selected ? core::indexAfterRemoval(*selected, indexes) : std::nullopt;
                    m_state.selectMany(kept, primary.value_or(kept.front()));
                }
                if (auto unattend = m_state.unattend(); unattend.options.imageIndex > 0) {
                    // Its edition is gone: Setup asks again (0).
                    unattend.options.imageIndex = core::indexAfterRemoval(unattend.options.imageIndex, indexes).value_or(0);
                    m_state.setUnattend(std::move(unattend));
                }
                m_events.succeeded(Str::ImagesDeleteDone, label);
            },
            Failure::Delete);
    });
}

void ImageController::cleanupMounts() {
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    if (!core::isElevated()) {
        m_events.needsAdmin(m_state.source() ? std::format(L"{} --page=images", core::quoteArgument(m_state.source()->path.wstring()))
                                             : std::wstring(L"--page=images"));
        return;
    }
    run(EngineOperation{EngineOperation::Kind::Cleaning, L"", m_state.settings().mountDirectory(), 0},
        [mountDir = m_state.settings().mountDirectory()](const core::TaskContext& task) -> Result<void> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto check = core::inspectMount(**dism, mountDir);
            if (!check) {
                return std::unexpected(check.error());
            }
            // A healthy mount is left alone (unmount it instead); everything else is repaired.
            if (check->state != core::MountState::Ok && check->state != core::MountState::Free) {
                auto repaired = core::repairMount(**dism, *check, task);
                if (!repaired) {
                    return std::unexpected(repaired.error());
                }
            }
            return (*dism)->cleanupMountpoints();
        },
        [this] {
            m_failedIndex.reset();
            m_events.succeeded(Str::ImagesCleanupMounts, L"");
            inspectMountFolder();
        },
        Failure::Cleanup);
}

void ImageController::cancel() {
    if (const auto& op = m_state.operation()) {
        op->cancel.cancel();
    }
}

void ImageController::adoptExistingMount() {
    if (!core::isElevated()) {
        return;
    }
    // Inspect the WinLove mount folder and bring it to a usable state (MountHealth.h):
    // Ok → restore, NeedsRemount → remount then restore, Invalid/ImageMissing → discard,
    // Orphaned → clear the folder. `before` is what we found, `after` the state after repair.
    struct Found {
        core::MountCheck before;
        core::MountCheck after;
        std::optional<core::VhdMountRecord> vhd; // D-141: WinLove's own virtual disk, attached again if need be
    };
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // Current mount folder first, then the pre-2026-09-28 one (C:\WinLove\mount).
    std::vector<std::filesystem::path> folders{m_state.settings().mountDirectory()};
    if (_wcsicmp(AppSettings::legacyMountDirectory().c_str(), folders.front().c_str()) != 0) {
        folders.push_back(AppSettings::legacyMountDirectory());
    }
    const std::filesystem::path vhdFolder = m_state.settings().vhdMountDirectory();
    m_state.engine().run<Found>(
        [folders, vhdFolder](const core::TaskContext& task) -> Result<Found> {
            // D-141: only the record of WinLove's own VHD folder is looked at (never another disk);
            // a reboot detached it: attached again (its changes are kept), as a WIM is remounted.
            std::optional<core::VhdMountRecord> vhd;
            if (auto check = core::inspectVhdMount(vhdFolder)) {
                log::info("app", std::format(L"virtual disk mount {}: {}", vhdFolder.wstring(), core::vhdMountStateName(check->state)));
                if (check->action != core::VhdAction::None) {
                    if (auto repaired = core::repairVirtualDisk(*check, task)) {
                        check = std::move(*repaired);
                    } else {
                        log::warn("app", L"virtual disk not attached again: " + describe(repaired.error()));
                    }
                }
                if (check->state == core::VhdMountState::Ok) {
                    vhd = check->record;
                }
            }
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            Result<core::MountCheck> before = std::unexpected(Error{});
            for (const auto& folder : folders) {
                std::error_code ec;
                if (folder != folders.front() && !std::filesystem::exists(folder, ec)) {
                    continue; // no legacy folder: nothing to look at
                }
                before = core::inspectMount(**dism, folder);
                if (!before || before->state != core::MountState::Free) {
                    break;
                }
            }
            if (!before) {
                return std::unexpected(before.error());
            }
            auto after = core::repairMount(**dism, *before, task);
            if (!after) {
                return std::unexpected(after.error());
            }
            return Found{std::move(*before), std::move(*after), std::move(vhd)};
        },
        [this, post, alive](Result<Found> result) {
            post([this, alive, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                if (!result) {
                    log::warn("app", describe(result.error()));
                    m_events.failed(Failure::Cleanup, result.error());
                    return;
                }
                const auto& [before, after, vhd] = *result;
                m_state.setMountFolder(after);
                log::info("app", std::format(L"mount folder {}: {} → {}", before.folder.wstring(),
                                             core::mountStateName(before.state), core::mountStateName(after.state)));
                if (before.action == core::MountAction::Discard) {
                    m_events.refused(Str::ImagesMountDiscarded);
                }
                if (vhd) {
                    // The virtual disk a previous run left mounted: its disk opens, it is the mounted image.
                    log::info("app", L"restoring virtual disk " + vhd->vhd.wstring() + L" on " + vhd->folder.wstring());
                    if (m_events.restored) {
                        m_events.restored(vhd->vhd, MountedImage{vhd->folder, vhd->vhd, 1, {}, false});
                    }
                    return;
                }
                if (after.state != core::MountState::Ok || !after.record) {
                    return;
                }
                const auto& m = *after.record;
                log::info("app", std::format(L"restoring mount {} <- {} [{}]", m.mountPath.wstring(),
                                             m.imagePath.wstring(), m.index));
                if (m_events.restored) {
                    m_events.restored(sourceForMountedImage(m.imagePath),
                                      MountedImage{m.mountPath, m.imagePath, m.index, {}, m.readOnly});
                }
            });
        },
        {});
}

void ImageController::inspectMountFolder() {
    if (!core::isElevated()) {
        return;
    }
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<core::MountCheck>(
        [mountDir = m_state.settings().mountDirectory()](const core::TaskContext&) -> Result<core::MountCheck> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            return core::inspectMount(**dism, mountDir);
        },
        [this, post, alive](Result<core::MountCheck> result) {
            post([this, alive, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a || !result) {
                    return;
                }
                m_state.setMountFolder(*result);
            });
        },
        {});
}

} // namespace wl::app
