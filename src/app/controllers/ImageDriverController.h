#pragma once
// D-052: the third-party drivers of the mounted image (the second tab of Sürücüler) and this
// PC's drivers as a source of new ones.
// - The list is read on the engine thread (a DISM session, DismGetDrivers: a second or two) when
//   the tab is first shown, once per mount. Ticking a driver queues RemoveDriver (target =
//   published name "oem3.inf"); a boot-critical one is high risk.
// - "Bu bilgisayardan al" exports this PC's driver store (pnputil) into <work>\host-drivers on
//   a thread of its own (minutes on a PC with many drivers) and scans it like any folder.
// D-134 (the boot tab, "Kurulum ortamı (boot.wim)"):
// - What the medium carries already (boot.wim's Setup image, the install image) is read from the
//   WIM file lists on the reader thread (loadMedia), once per source.
// - "Bu bilgisayardan al" on that tab ticks what Setup can use of this PC's drivers: storage
//   controllers and wired network cards of the medium's architecture (setupPick).
// - A storage driver ticked for boot.wim is queued for the mounted image as well: the installed
//   Windows boots from the same controller (toggleBoot). Without a mount the ISO page warns
//   (storageOnlyInBoot).
#include "app/state/AppState.h"

#include <functional>
#include <memory>

namespace wl::app {

class ImageDriverController {
public:
    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        // `picked`: the INFs ticked for the setup media (an export started from the boot tab).
        std::function<void(int packages, const std::filesystem::path& folder, std::size_t picked)> exported;
        std::function<void(const Error&)> failed;
    };
    ImageDriverController(AppState& state, Events events);
    ~ImageDriverController();

    void load(bool force = false);
    [[nodiscard]] bool queuedForRemoval(const core::DriverEntry& driver) const;
    void toggle(const core::DriverEntry& driver);
    // D-152: the page's Sıfırla — drivers to add and to remove leave the queue (one undo step), and
    // none is ticked for the setup media's boot.wim (that list is not in the queue: no undo for it).
    // The scanned folders stay listed, unticked.
    void resetChanges();
    [[nodiscard]] int removalCount() const;

    [[nodiscard]] bool exporting() const noexcept { return m_exporting; }
    // No-op while one runs. `forSetup`: also tick what setupPick() picks for boot.wim.
    void exportHost(bool forSetup = false);
    [[nodiscard]] std::filesystem::path hostFolder() const;

    // D-134 -----------------------------------------------------------------------------------
    void loadMedia(bool force = false); // no-op without a source, or when read (being read) for it
    // The setup media's architecture: boot.wim's Setup image, else the install image (Unknown: no source).
    [[nodiscard]] static core::Architecture mediaArchitecture(const AppState& state);
    // Ticks / unticks `infs` for boot.wim (one notification). A storage driver ticked while an image
    // is mounted is queued for the image too, unless it is already: returns how many were.
    int setBoot(const std::vector<core::DriverInf>& infs, bool on);
    bool toggleBoot(const core::DriverInf& inf) { return setBoot({inf}, !m_state.isBootDriver(inf.path)) > 0; }
    // Of `infs`, the ones the setup media can use and does not have yet: storage + wired network
    // (core::setupUseful), fitting `arch`, not one of `present` (core::containsDriver).
    [[nodiscard]] static std::vector<std::filesystem::path> setupPick(const std::vector<core::DriverInf>& infs,
                                                                      core::Architecture arch,
                                                                      const std::vector<core::WimDriver>& present);
    // The storage drivers ticked for boot.wim that the installed Windows would not get: not queued for
    // the image, not in the last Uygula, not in any edition of the install image (when that is known).
    [[nodiscard]] static std::vector<std::filesystem::path> storageOnlyInBoot(const AppState& state);

    [[nodiscard]] static core::ops::Operation operationFor(const core::DriverEntry& driver);
    [[nodiscard]] static core::ops::Operation addOperationFor(const core::DriverInf& inf);

private:
    AppState& m_state;
    Events m_events;
    bool m_exporting = false;
    std::filesystem::path m_mediaFor; // the source loadMedia last started for
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<core::TaskRunner> m_background; // last: joined first
};

} // namespace wl::app
