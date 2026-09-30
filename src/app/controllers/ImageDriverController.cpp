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

int ImageDriverController::removalCount() const {
    return static_cast<int>(m_state.changes().count(OpKind::RemoveDriver));
}

std::filesystem::path ImageDriverController::hostFolder() const {
    return m_state.settings().workRoot / L"host-drivers";
}

void ImageDriverController::exportHost() {
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
        [this, post, alive, folder, infs](Result<int> result) {
            post([this, alive, folder, infs, result = std::move(result)] {
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
                if (!infs->empty()) {
                    m_state.addDriverScan(folder, std::move(*infs));
                }
                if (m_events.exported) {
                    m_events.exported(*result, folder);
                }
            });
        });
}

} // namespace wl::app
