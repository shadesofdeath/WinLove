#include "app/controllers/ImageDriverController.h"

#include "base/Log.h"
#include "core/image/DriverInf.h"
#include "core/image/dism/Dism.h"
#include "core/system/HostExport.h"

#include <format>

namespace wl::app {

using core::ops::OpKind;
using core::ops::Operation;
using core::ops::Risk;

ImageDriverController::ImageDriverController(AppState& state, Events events)
    : m_state(state), m_events(std::move(events)), m_background(std::make_unique<core::TaskRunner>()) {}

ImageDriverController::~ImageDriverController() {
    *m_alive = false;
    m_background.reset();
}

Operation ImageDriverController::operationFor(const core::DriverEntry& driver) {
    Operation op{OpKind::RemoveDriver, driver.publishedName, driver.originalFileName};
    // A boot-critical driver (storage, chipset) gone from the store can leave the installed
    // system unable to find its disk.
    op.risk = driver.bootCritical ? Risk::High : Risk::Medium;
    return op;
}

void ImageDriverController::load(bool force) {
    const auto& mounted = m_state.mounted();
    if (!mounted) {
        return;
    }
    const auto& current = m_state.imageDrivers();
    if (!force && current && current->mountDir == mounted->mountDir &&
        current->status != AppState::ImageDrivers::Status::Failed) {
        return;
    }
    const std::filesystem::path mountDir = mounted->mountDir;
    m_state.setImageDrivers(AppState::ImageDrivers{AppState::ImageDrivers::Status::Loading, mountDir, {}, {}});
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    m_state.engine().run<std::vector<core::DriverEntry>>(
        [mountDir](const core::TaskContext&) -> Result<std::vector<core::DriverEntry>> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            auto session = (*dism)->openSession(mountDir);
            if (!session) {
                return std::unexpected(session.error());
            }
            return (*session)->drivers();
        },
        [this, post, alive, mountDir](Result<std::vector<core::DriverEntry>> result) {
            post([this, alive, mountDir, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                const auto& mounted = m_state.mounted();
                if (!mounted || mounted->mountDir != mountDir) {
                    return;
                }
                if (!result) {
                    log::error("app", describe(result.error()));
                    m_state.setImageDrivers(
                        AppState::ImageDrivers{AppState::ImageDrivers::Status::Failed, mountDir, {}, result.error()});
                    return;
                }
                log::info("app", std::format(L"image drivers: {} third-party package(s)", result->size()));
                m_state.setImageDrivers(
                    AppState::ImageDrivers{AppState::ImageDrivers::Status::Ready, mountDir, std::move(*result), {}});
            });
        });
}

bool ImageDriverController::queuedForRemoval(const core::DriverEntry& driver) const {
    return m_state.changes().find(OpKind::RemoveDriver, driver.publishedName) != nullptr;
}

void ImageDriverController::toggle(const core::DriverEntry& driver) {
    if (!m_state.unqueue(OpKind::RemoveDriver, driver.publishedName)) {
        m_state.queue(operationFor(driver));
    }
}

void ImageDriverController::resetChanges() {
    m_state.unqueueIf([](const Operation& op) { return op.kind == OpKind::AddDriver || op.kind == OpKind::RemoveDriver; });
    if (!m_state.bootDrivers().empty()) {
        const std::vector<std::filesystem::path> ticked = m_state.bootDrivers(); // a copy: the call edits the list
        m_state.setBootDrivers(ticked, false);
    }
}

int ImageDriverController::removalCount() const {
    return static_cast<int>(m_state.changes().count(OpKind::RemoveDriver));
}

std::filesystem::path ImageDriverController::hostFolder() const {
    return m_state.settings().workRoot / L"host-drivers";
}

Operation ImageDriverController::addOperationFor(const core::DriverInf& inf) {
    Operation op{OpKind::AddDriver, inf.path.wstring(), inf.className};
    op.risk = Risk::Low;
    op.sizeDelta = static_cast<std::int64_t>(inf.size);
    return op;
}

void ImageDriverController::loadMedia(bool force) {
    const auto& source = m_state.source();
    if (!source) {
        return;
    }
    const auto& current = m_state.mediaDrivers();
    // Read once per source, a failure too: the page asks again on every Change::Drivers, which the
    // failure itself is (a retry is `force`).
    if (!force && current && current->source == source->path) {
        return;
    }
    if (!force && !current && m_mediaFor == source->path) {
        return; // started, not back yet
    }
    m_mediaFor = source->path;
    const core::SourceInfo info = *source;
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    auto read = std::make_shared<Result<core::MediaDrivers>>(core::MediaDrivers{});
    // The file lists only (no DISM): the reader thread, not the engine's queue.
    m_state.reader().run<bool>(
        [info, read](const core::TaskContext&) -> Result<bool> {
            *read = core::readMediaDrivers(info);
            return true;
        },
        [this, post, alive, path = info.path, read](Result<bool>) {
            if (!post) {
                return;
            }
            post([this, alive, path, read] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                if (!m_state.source() || m_state.source()->path != path) {
                    return; // another source since
                }
                m_mediaFor.clear();
                if (!*read) {
                    log::warn("app", L"setup media drivers: " + describe(read->error()));
                    m_state.setMediaDrivers(AppState::MediaDrivers{AppState::MediaDrivers::Status::Failed, path, {}, read->error()});
                    return;
                }
                m_state.setMediaDrivers(AppState::MediaDrivers{AppState::MediaDrivers::Status::Ready, path, std::move(**read), {}});
            });
        });
}

core::Architecture ImageDriverController::mediaArchitecture(const AppState& state) {
    if (const auto& media = state.mediaDrivers(); media && media->status == AppState::MediaDrivers::Status::Ready &&
                                                  media->drivers.architecture != core::Architecture::Unknown) {
        return media->drivers.architecture;
    }
    const auto& source = state.source();
    if (!source) {
        return core::Architecture::Unknown;
    }
    if (source->boot && !source->boot->images.empty()) {
        const int boot = source->boot->header.bootIndex > 0 ? static_cast<int>(source->boot->header.bootIndex)
                                                            : source->boot->images.back().index;
        for (const auto& image : source->boot->images) {
            if (image.index == boot) {
                return image.architecture;
            }
        }
    }
    return source->install.images.empty() ? core::Architecture::Unknown : source->install.images.front().architecture;
}

int ImageDriverController::setBoot(const std::vector<core::DriverInf>& infs, bool on) {
    std::vector<std::filesystem::path> paths;
    for (const auto& inf : infs) {
        paths.push_back(inf.path);
    }
    m_state.setBootDrivers(paths, on);
    // The installed Windows boots from the same controller: a storage driver Setup needs to see the
    // disk is one the image needs as well (D-134). Taking it off boot.wim leaves the image's queue be.
    if (!on || !m_state.mounted()) {
        return 0;
    }
    int queued = 0;
    const auto batch = m_state.batchQueue();
    for (const auto& inf : infs) {
        if (core::setupRole(inf) != core::SetupRole::Storage ||
            m_state.changes().find(OpKind::AddDriver, inf.path.wstring()) != nullptr) {
            continue;
        }
        m_state.queue(addOperationFor(inf));
        ++queued;
        log::info("app", L"storage driver for boot.wim, queued for the image too: " + inf.path.filename().wstring());
    }
    return queued;
}

std::vector<std::filesystem::path> ImageDriverController::setupPick(const std::vector<core::DriverInf>& infs,
                                                                    core::Architecture arch,
                                                                    const std::vector<core::WimDriver>& present) {
    std::vector<std::filesystem::path> picked;
    for (const auto& inf : infs) {
        if (core::setupUseful(core::setupRole(inf)) && core::fitsArchitecture(inf, arch) && !core::containsDriver(present, inf)) {
            picked.push_back(inf.path);
        }
    }
    return picked;
}

std::vector<std::filesystem::path> ImageDriverController::storageOnlyInBoot(const AppState& state) {
    std::vector<std::filesystem::path> missing;
    const auto& media = state.mediaDrivers();
    const bool installKnown = media && media->status == AppState::MediaDrivers::Status::Ready && media->drivers.installRead;
    const auto& run = state.applyRun();
    for (const auto& path : state.bootDrivers()) {
        const auto it = std::ranges::find(state.driverScan().infs, path, &core::DriverInf::path);
        // An INF from a preset whose folder was not scanned: its class is read from the file.
        const core::DriverInf inf = it != state.driverScan().infs.end() ? *it : core::parseInf(path);
        if (core::setupRole(inf) != core::SetupRole::Storage) {
            continue;
        }
        const std::wstring target = path.wstring();
        if (state.changes().find(OpKind::AddDriver, target) ||
            (run && run->stage == AppState::ApplyRun::Stage::Done && run->changes.find(OpKind::AddDriver, target)) ||
            (installKnown && core::containsDriver(media->drivers.install, inf))) {
            continue;
        }
        missing.push_back(path);
    }
    return missing;
}

void ImageDriverController::exportHost(bool forSetup) {
    if (m_exporting) {
        return;
    }
    m_exporting = true;
    const std::filesystem::path folder = hostFolder();
    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    auto infs = std::make_shared<std::vector<core::DriverInf>>();
    m_background->run<int>(
        [folder, infs](const core::TaskContext& task) -> Result<int> {
            // A fresh export: what an earlier one left would mix two PCs' drivers.
            std::error_code ec;
            std::filesystem::remove_all(folder, ec);
            auto n = core::exportHostDrivers(folder, task);
            if (n) {
                *infs = core::scanDrivers(folder);
            }
            return n;
        },
        [this, post, alive, folder, infs, forSetup](Result<int> result) {
            post([this, alive, folder, infs, forSetup, result = std::move(result)] {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                m_exporting = false;
                if (!result) {
                    log::error("app", describe(result.error()));
                    if (m_events.failed) {
                        m_events.failed(result.error());
                    }
                    return;
                }
                std::vector<std::filesystem::path> picked;
                std::vector<core::DriverInf> pickedInfs;
                if (forSetup) {
                    const auto& media = m_state.mediaDrivers();
                    static const std::vector<core::WimDriver> kNone;
                    const auto& present =
                        media && media->status == AppState::MediaDrivers::Status::Ready ? media->drivers.boot : kNone;
                    picked = setupPick(*infs, mediaArchitecture(m_state), present);
                    for (const auto& inf : *infs) {
                        if (std::ranges::find(picked, inf.path) != picked.end()) {
                            pickedInfs.push_back(inf);
                        }
                    }
                }
                if (!infs->empty()) {
                    m_state.addDriverScan(folder, std::move(*infs));
                }
                if (!picked.empty()) {
                    setBoot(pickedInfs, true); // the storage ones go to the mounted image as well (D-134)
                    std::wstring names;
                    for (const auto& p : picked) {
                        names += (names.empty() ? L"" : L", ") + p.filename().wstring();
                    }
                    log::info("app", std::format(L"this PC's drivers for the setup media: {} ticked ({})", picked.size(), names));
                }
                if (m_events.exported) {
                    m_events.exported(*result, folder, picked.size());
                }
            });
        });
}

} // namespace wl::app
