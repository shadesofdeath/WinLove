#pragma once
// D-144: "Bitince ISO oluştur" on the Apply page. Chosen before a run (AppSettings::isoAfterApply; the
// file it writes known then: ISO Oluştur's folder and the Apply page's file name, else ISO Oluştur's
// default name). When the run has saved the image (and unmounted it, the source read again) the setup
// ISO is built through the one ISO engine there is (IsoController, with ISO Oluştur's first values);
// when the run did not save, the run's card says why no ISO was made. The state lives in the run
// (AppState::ApplyRun::autoIso): a shell built again keeps it, the Apply page and the overview read it.
#include "app/controllers/IsoController.h"
#include "app/state/AppState.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace wl::app {

// Whether a finished run lets the ISO start (Skip::None) or why not — pure, the unit tests' way in.
// A saved run starts it (every step done, or some skipped and the image saved anyway — the way the
// ISO page would be used next); a run that could not start, failed to save, was stopped, held after a
// failed step, kept unsaved at the pause or left a further edition unsaved does not.
[[nodiscard]] AppState::ApplyRun::AutoIso::Skip autoIsoSkip(const AppState::ApplyRun& run);

class AutoIsoController {
public:
    struct Hooks {
        std::function<std::optional<IsoController::Blocker>()> blocker; // IsoController::blocker()
        std::function<void(IsoController::Request)> start;              // IsoController::start
        // D-084: the editions the welcome's script is missing from, when the answer file needs it
        // there (the ISO page's check before a build); empty: none missing.
        std::function<std::wstring()> welcomeMissing;
    };

    AutoIsoController(AppState& state, Hooks hooks);
    ~AutoIsoController();
    AutoIsoController(const AutoIsoController&) = delete;
    AutoIsoController& operator=(const AutoIsoController&) = delete;

    // Why the option cannot be offered for this source (nullopt: it can): ISO Oluştur's own reasons
    // about the source and the answer file (a VHD, a lone WIM, an answer Setup would refuse).
    [[nodiscard]] std::optional<IsoController::Blocker> unavailable() const;
    // The file a run would write now.
    [[nodiscard]] std::filesystem::path output() const;
    // The Apply page's "Değiştir…": the folder becomes ISO Oluştur's folder too (settings), the name is
    // kept for this source.
    void setOutput(const std::filesystem::path& file);
    // Right after ApplyController::start: with the option on and an ISO possible, the run carries it.
    void attach();
    // ApplyController's finished event (the run is Done, the source read again): the ISO starts, or
    // the run says why not.
    void runFinished();

private:
    void isoChanged(); // Change::Iso: the ISO's end goes into the run

    AppState& m_state;
    Hooks m_hooks;
    std::size_t m_subscription = 0;
    std::wstring m_name;                 // the picked file name, for…
    std::filesystem::path m_nameSource;  // …this source
};

} // namespace wl::app
