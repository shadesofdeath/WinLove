#include "app/controllers/ImageController.h"

#include "base/Log.h"
#include "base/Path.h"
#include "core/image/UdfImage.h"
#include "core/image/dism/Dism.h"
#include "core/image/dism/DismErrors.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/wim/WimGapi.h"
#include "core/system/Privileges.h"
#include "ui/anim/Tween.h"

#include <cmath>
#include <format>

namespace wl::app {

std::filesystem::path sourceForMountedImage(const std::filesystem::path& imagePath) {
    if (_wcsicmp(imagePath.parent_path().filename().c_str(), L"sources") == 0) {
        return imagePath.parent_path().parent_path();
    }
    return imagePath;
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

bool ImageController::canMount() const {
    return m_state.source() && !isEsdSource() && !m_state.mounted() && !busy();
}

bool ImageController::canDelete() const {
    const auto& source = m_state.source();
    return source && !busy() && !m_state.mounted() &&
           (source->format == core::ImageFormat::Wim || source->format == core::ImageFormat::Folder) &&
           source->install.images.size() > 1 && !isEsdSource();
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

void ImageController::run(EngineOperation op, Work work, std::function<void()> onSuccess, Failure failure) {
    op.startedMs = ui::nowMs();
    const core::CancelToken cancel = op.cancel;
    const int index = op.index;
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
        [this, post, alive, onSuccess = std::move(onSuccess), failure, index](Result<bool> result) {
            post([this, alive, onSuccess, failure, index, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_state.endOperation();
                if (!result) {
                    log::error("app", describe(result.error()));
                    if (result.error().code == ErrorCode::Cancelled) {
                        m_events.refused(Str::ImagesCancelledToast);
                        return;
                    }
                    m_failedIndex = index;
                    m_events.failed(failure, result.error(), index);
                    inspectMountFolder(); // show what the failure left behind
                    return;
                }
                m_failedIndex.reset();
                onSuccess();
            });
        },
        {});
}

void ImageController::withWritableSource(int index, std::function<void()> next) {
    const auto& source = m_state.source();
    if (source->format != core::ImageFormat::Iso) {
        next();
        return;
    }
    // DISM and wimgapi need real files: copy the ISO to the work folder once (resumable).
    const std::filesystem::path iso = source->path;
    const std::filesystem::path folder = m_state.settings().workDirectoryFor(iso);
    auto opened = std::make_shared<std::optional<core::SourceInfo>>();
    run(EngineOperation{EngineOperation::Kind::Preparing, iso.filename().wstring(), folder, index},
        [iso, folder, opened](const core::TaskContext& task) -> Result<void> {
            auto image = core::UdfImage::open(iso);
            if (!image) {
                return std::unexpected(image.error());
            }
            if (auto r = image->extractAll(folder, task); !r) {
                return r;
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
    if (isEsdSource()) {
        m_events.refused(Str::ImagesEsdNoMount);
        return;
    }
    if (!core::isElevated()) {
        // Relaunch through UAC and continue: reopen this source and mount the same index.
        m_events.needsAdmin(std::format(L"{} --page=images --mount={}", core::quoteArgument(m_state.source()->path.wstring()), index));
        return;
    }
    withWritableSource(index, [this, index] {
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

void ImageController::unmount(bool commit) {
    const auto mounted = m_state.mounted();
    if (!mounted || busy()) {
        return;
    }
    auto recovered = std::make_shared<bool>(false);
    run(EngineOperation{EngineOperation::Kind::Unmounting, mounted->edition, mounted->mountDir, mounted->index},
        [dir = mounted->mountDir, commit, recovered](const core::TaskContext& task) -> Result<void> {
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
            return {};
        },
        [this, recovered] {
            m_state.setMounted(std::nullopt);
            m_events.succeeded(*recovered ? Str::ImagesUnmountRecovered : Str::ImagesUnmountedToast, L"");
            inspectMountFolder();
        },
        Failure::Unmount);
}

void ImageController::exportIndex(int index, const std::filesystem::path& destination) {
    if (busy()) {
        m_events.refused(Str::ImagesBusy);
        return;
    }
    withWritableSource(index, [this, index, destination] {
        const auto wim = installWimPath();
        run(EngineOperation{EngineOperation::Kind::Exporting, editionName(index), destination, index},
            [wim = *wim, index, destination](const core::TaskContext& task) -> Result<void> {
                return core::exportImage(wim, index, destination, core::WimCompression::Lzx, task);
            },
            [this, destination] { m_events.succeeded(Str::ImagesExportedToast, destination.filename().wstring()); },
            Failure::Export);
    });
}

void ImageController::convertEsd(const std::filesystem::path& destination) {
    if (busy() || !isEsdSource()) {
        m_events.refused(busy() ? Str::ImagesBusy : Str::ImagesEsdOnly);
        return;
    }
    withWritableSource(m_state.selectedIndex().value_or(1), [this, destination] {
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

void ImageController::deleteIndex(int index) {
    if (!canDelete()) {
        m_events.refused(busy() ? Str::ImagesBusy : Str::ImagesReadOnlySource);
        return;
    }
    const auto wim = installWimPath();
    const std::filesystem::path sourcePath = m_state.source()->path;
    auto reopened = std::make_shared<std::optional<core::SourceInfo>>();
    run(EngineOperation{EngineOperation::Kind::Deleting, editionName(index), *wim, index},
        [wim = *wim, index, sourcePath, reopened](const core::TaskContext&) -> Result<void> {
            if (auto r = core::deleteImage(wim, index); !r) {
                return r;
            }
            auto info = core::openSource(sourcePath);
            if (!info) {
                return std::unexpected(info.error());
            }
            *reopened = std::move(*info);
            return {};
        },
        [this, reopened] {
            m_state.setSource(std::move(**reopened));
            m_events.succeeded(Str::ImagesDeleteDone, L"");
        },
        Failure::Delete);
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
    };
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // Current mount folder first, then the pre-2026-09-28 one (C:\WinLove\mount).
    std::vector<std::filesystem::path> folders{m_state.settings().mountDirectory()};
    if (_wcsicmp(AppSettings::legacyMountDirectory().c_str(), folders.front().c_str()) != 0) {
        folders.push_back(AppSettings::legacyMountDirectory());
    }
    m_state.engine().run<Found>(
        [folders](const core::TaskContext& task) -> Result<Found> {
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
            return Found{std::move(*before), std::move(*after)};
        },
        [this, post, alive](Result<Found> result) {
            post([this, alive, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                if (!result) {
                    log::warn("app", describe(result.error()));
                    m_events.failed(Failure::Cleanup, result.error(), 0);
                    return;
                }
                const auto& [before, after] = *result;
                m_state.setMountFolder(after);
                log::info("app", std::format(L"mount folder {}: {} → {}", before.folder.wstring(),
                                             core::mountStateName(before.state), core::mountStateName(after.state)));
                if (before.action == core::MountAction::Discard) {
                    m_events.refused(Str::ImagesMountDiscarded);
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
