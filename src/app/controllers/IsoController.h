#pragma once
// P06 logic (docs/pages/06-iso.md): build a bootable ISO from the open source. Setup folders are
// used as they are; an ISO source is first extracted to the work folder (resumable). Optional
// repack of install.wim (only inside WinLove's work folder), then IMAPI2FS, optional SHA-256.
// The requirement bypasses of the answers can also go into Setup's own image (D-038): a copy of
// sources\boot.wim is patched and takes the place of the folder's file in the ISO.
#include "app/state/AppState.h"
#include "core/image/BootImage.h"
#include "core/image/WorkCopy.h"
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
        bool secureBoot2023 = false; // D-094: the boot manager signed by Windows UEFI CA 2023
        // D-047: a setup stick instead of an ISO file (same pipeline, the last step writes the
        // disk: output / sha256 / noPrompt do not apply).
        struct UsbTarget {
            int disk = -1;
            std::wstring identity; // core::UsbDisk::identity() as picked
            std::wstring name;     // for the log and the result
            core::UsbScheme scheme = core::UsbScheme::MbrBiosUefi;
        };
        std::optional<UsbTarget> usb;
        // An ISO source's work copy that changed since or is another ISO's (core::WorkCopyState):
        // Ask goes through Events::workCopyChoice, which starts again with Fresh or Keep.
        enum class WorkCopy : std::uint8_t { Ask, Fresh, Keep };
        WorkCopy workCopy = WorkCopy::Ask;
        // D-144: "Bitince ISO oluştur". The previous Setup when an edition has no WinRE, decided as the
        // ISO page decides it (its box goes on by itself) — on the engine thread, before the build.
        bool legacySetupAuto = false;
        bool afterApply = false; // AppState::IsoRun::afterApply
    };
    // Why a build cannot start now (nullopt = it can).
    // UnattendInvalid: "ISO'ya ekle" is on and the answer file has a value Setup would reject.
    // VirtualDisk (D-141): a VHD / VHDX source holds an installed Windows, not setup media.
    enum class Blocker : std::uint8_t { NoSource, WimOnly, Mounted, Busy, UnattendInvalid, VirtualDisk };

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(const Error&)> failed;
        std::function<void(const core::IsoResult&, const std::filesystem::path&, bool openFolder)> finished;
        std::function<void(std::wstring relaunchArgs)> needsAdmin; // USB: diskpart needs an elevated process
        // See ImageController::Events::workCopyChoice; proceed(true) = Request::WorkCopy::Fresh.
        std::function<void(core::WorkCopyState state, std::filesystem::path folder, std::function<void(bool fresh)> proceed)>
            workCopyChoice;
    };

    IsoController(AppState& state, Events events);
    ~IsoController();

    [[nodiscard]] std::optional<Blocker> blocker() const;
    // D-144: what blocker() says about the source and the answer file alone (a mounted image and a
    // running Apply are what a run that ends with an ISO has until it saves).
    [[nodiscard]] static std::optional<Blocker> sourceBlocker(const AppState& state);
    // ISO Oluştur's first values, the ones its page shows before anything is changed (the page and
    // "Bitince ISO oluştur" share them): "<source>_WinLove.iso" in the last ISO folder (else the
    // Desktop), the source's volume label (else "WINLOVE"), UEFI + BIOS, SHA-256, the answers'
    // bypasses into boot.wim, the downloaded media update when its box is on.
    [[nodiscard]] static std::wstring defaultFileName(const AppState& state);
    [[nodiscard]] static std::filesystem::path defaultFolder(const AppState& state);
    [[nodiscard]] static std::wstring defaultLabel(const core::SourceInfo& source);
    // The label AppState::isoChoices holds, else the source's (defaultLabel).
    [[nodiscard]] static std::wstring chosenLabel(const AppState& state);
    // "Bitince ISO oluştur"'s request: the options the ISO page and "ISO seçenekleri…" share
    // (AppState::isoChoices; the media update's box), the run's saved work copy, no folder opened.
    [[nodiscard]] static Request defaultRequest(const AppState& state, const std::filesystem::path& output);
    // Repacking rewrites the setup files: only allowed for folders WinLove extracted itself.
    [[nodiscard]] bool canRepack() const { return canRepack(m_state); }
    [[nodiscard]] static bool canRepack(const AppState& state);
    [[nodiscard]] bool running() const;
    void start(Request request);
    void cancel();

    // What goes into boot.wim: the checks the answers switch off (with `bypass`, the ISO page's box;
    // independent of the answer file going into the ISO) and, always, the drivers ticked on
    // Sürücüler › Kurulum ortamı (D-134: they used to ride on the bypass box and went missing
    // without a word when it was cleared). Empty: nothing to write.
    [[nodiscard]] static core::BootPatch bootPatch(const AppState& state, bool bypass = true);
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
