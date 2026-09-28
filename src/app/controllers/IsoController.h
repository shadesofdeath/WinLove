#pragma once
// P06 logic (docs/pages/06-iso.md): build a bootable ISO from the open source. Setup folders are
// used as they are; an ISO source is first extracted to the work folder (resumable). Optional
// repack of install.wim (only inside WinLove's work folder), then IMAPI2FS, optional SHA-256.
#include "app/state/AppState.h"
#include "core/iso/IsoBuilder.h"

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
    };
    // Why a build cannot start now (nullopt = it can).
    enum class Blocker : std::uint8_t { NoSource, WimOnly, Mounted, Busy };

    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(const Error&)> failed;
        std::function<void(const core::IsoResult&, const std::filesystem::path&, bool openFolder)> finished;
    };

    IsoController(AppState& state, Events events);
    ~IsoController();

    [[nodiscard]] std::optional<Blocker> blocker() const;
    // Repacking rewrites the setup files: only allowed for folders WinLove extracted itself.
    [[nodiscard]] bool canRepack() const;
    [[nodiscard]] bool running() const;
    void start(Request request);
    void cancel();

private:
    AppState& m_state;
    Events m_events;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
