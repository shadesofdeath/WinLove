#pragma once
// P06 logic (docs/pages/06-iso.md): build a bootable ISO from the open source. Setup folders are
// used as they are; an ISO source is first extracted to the work folder (resumable). Optional
// repack of install.wim (only inside WinLove's work folder), then IMAPI2FS, optional SHA-256.
// The requirement bypasses of the answers can also go into Setup's own image (D-038): a copy of
// sources\boot.wim is patched and takes the place of the folder's file in the ISO.
#include "app/state/AppState.h"
#include "core/image/BootImage.h"
#include "core/iso/IsoBuilder.h"
#include "core/usb/UsbMedia.h"

#include <functional>
#include <memory>

namespace wl::app {

class IsoController {
public:
    enum class Repack : std::uint8_t { AsIs, Lzx, Xpress, Esd };
    struct Request {
        std::filesystem::path output;
        std::wstring label;
        core::BootMode boot = core::BootMode::UefiAndBios;
        bool noPrompt = false;
        Repack repack = Repack::AsIs;
        bool sha256 = true;
        bool openFolder = true;
        bool bootBypass = true; // the answers' requirement bypasses also go into boot.wim
        bool legacySetup = false; // D-074: the media boots into the previous Setup (24H2+)
        bool mediaUpdate = false; // D-080: boot.wim and the setup files brought up to date (AppState::mediaUpdate)
        // D-047: a setup stick instead of an ISO file (same pipeline, the last step writes the
        // disk: output / sha256 / noPrompt do not apply).
        struct UsbTarget {
            int disk = -1;
            std::wstring identity; // core::UsbDisk::identity() as picked
            std::wstring name;     // for the log and the result
            core::UsbScheme scheme = core::UsbScheme::MbrBiosUefi;
        };
        std::optional<UsbTarget> usb;
    };
    // Why a build cannot start now (nullopt = it can).
    // UnattendInvalid: "ISO'ya ekle" is on and the answer file has a value Setup would reject.
    enum class Blocker : std::uint8_t { NoSource, WimOnly, Mounted, Busy, UnattendInvalid };

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(const Error&)> failed;
        std::function<void(const core::IsoResult&, const std::filesystem::path&, bool openFolder)> finished;
        std::function<void(std::wstring relaunchArgs)> needsAdmin; // USB: diskpart needs an elevated process
    };

    IsoController(AppState& state, Events events);
    ~IsoController();

    [[nodiscard]] std::optional<Blocker> blocker() const;
    // Repacking rewrites the setup files: only allowed for folders WinLove extracted itself.
    [[nodiscard]] bool canRepack() const;
    [[nodiscard]] bool running() const;
    void start(Request request);
    void cancel();

    // What `bootBypass` writes: the checks the answers switch off (empty: none, nothing to write).
    // It does not depend on the answer file going into the ISO.
    [[nodiscard]] static core::BootPatch bootPatch(const AppState& state);
    // Patches a boot.wim in a mount folder. The default is core::patchBootImage through DISM
    // (elevated process); tests put their own in.
    using BootPatcher = std::function<Result<core::BootPatchReport>(
        const std::filesystem::path& bootWim, const std::filesystem::path& mountDir, const core::BootPatch&,
        const core::TaskContext&)>;
    void setBootPatcher(BootPatcher patcher) { m_patcher = std::move(patcher); }
    // Writes the stick (default core::writeUsb; tests put their own in).
    using UsbWriter = std::function<Result<core::UsbResult>(const core::UsbOptions&, const core::TaskContext&)>;
    void setUsbWriter(UsbWriter writer) { m_usbWriter = std::move(writer); }

private:
    AppState& m_state;
    Events m_events;
    BootPatcher m_patcher;
    UsbWriter m_usbWriter;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
