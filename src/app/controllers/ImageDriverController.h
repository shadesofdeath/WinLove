#pragma once
// D-052: the third-party drivers of the mounted image (the second tab of Sürücüler) and this
// PC's drivers as a source of new ones.
// - The list is read on the engine thread (a DISM session, DismGetDrivers: a second or two) when
//   the tab is first shown, once per mount. Ticking a driver queues RemoveDriver (target =
//   published name "oem3.inf"); a boot-critical one is high risk.
// - "Bu bilgisayardan al" exports this PC's driver store (pnputil) into <work>\host-drivers on
//   a thread of its own (minutes on a PC with many drivers) and scans it like any folder.
#include "app/state/AppState.h"

#include <functional>
#include <memory>

namespace wl::app {

class ImageDriverController {
public:
    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(int packages, const std::filesystem::path& folder)> exported;
        std::function<void(const Error&)> failed;
    };
    ImageDriverController(AppState& state, Events events);
    ~ImageDriverController();

    void load(bool force = false);
    [[nodiscard]] bool queuedForRemoval(const core::DriverEntry& driver) const;
    void toggle(const core::DriverEntry& driver);
    [[nodiscard]] int removalCount() const;

    [[nodiscard]] bool exporting() const noexcept { return m_exporting; }
    void exportHost(); // no-op while one runs
    [[nodiscard]] std::filesystem::path hostFolder() const;

    [[nodiscard]] static core::ops::Operation operationFor(const core::DriverEntry& driver);

private:
    AppState& m_state;
    Events m_events;
    bool m_exporting = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_background; // last: joined first
};

} // namespace wl::app
