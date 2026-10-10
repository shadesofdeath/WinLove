#pragma once
// P05 logic (docs/pages/05-apply.md): ChangeSet → plan summary; start = run the plan on the
// mounted image, then commit + unmount (core::ops::runApplyJob) on the engine thread; live
// progress into AppState::applyRun; cancel between steps. After a committed run the edition is
// re-read from the WIM ("Sonra" size) and the source is refreshed.
#include "app/state/AppState.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace wl::app {

class ApplyController {
public:
    struct Events {
        std::function<void(std::function<void()>)> postToUi;
        std::function<void(core::SourceInfo)> sourceChanged; // re-read source after commit
        std::function<void(const Error&)> failed;             // could not start
        std::function<void()> finished;                       // Done (success or with warnings)
    };

    ApplyController(AppState& state, Events events);
    ~ApplyController();

    [[nodiscard]] core::ops::ApplyPlan currentPlan() const;
    // High-risk operations of the current queue (the confirm dialog lists them).
    [[nodiscard]] std::vector<core::ops::Operation> highRisk() const;
    [[nodiscard]] bool canStart() const;   // mounted, idle, something queued
    [[nodiscard]] bool running() const;
    void start();
    // Running: the rest is skipped. Paused (D-106): the edition is left unsaved — the mounted one
    // stays mounted, a further one is discarded — and the editions after it are skipped.
    void cancel();
    // D-106 (AppSettings::pauseBeforeSave): before each save the run waits, the image mounted, for
    // changes by hand in the mount folder; resume() saves and goes on.
    [[nodiscard]] bool paused() const;
    void resume();

    // D-055: other editions of the mounted WIM the queue also goes to (their indexes).
    struct Edition {
        int index = 0;
        std::wstring name;
    };
    [[nodiscard]] std::vector<Edition> otherEditions() const; // of the source, the mounted one left out
    [[nodiscard]] const std::vector<int>& extraEditions() const noexcept { return m_extra; }
    void setExtraEdition(int index, bool on);

private:
    // Where the engine thread waits at a pause until the UI says save (true) or not (false).
    struct Gate {
        std::mutex mutex;
        std::condition_variable cv;
        std::optional<bool> decision;
        void decide(bool save);
        [[nodiscard]] bool wait();
    };

    AppState& m_state;
    Events m_events;
    std::vector<int> m_extra;
    std::shared_ptr<Gate> m_gate = std::make_shared<Gate>(); // a new one for each run
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
