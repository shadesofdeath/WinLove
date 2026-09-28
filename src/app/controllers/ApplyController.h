#pragma once
// P05 logic (docs/pages/05-apply.md): ChangeSet → plan summary; start = run the plan on the
// mounted image, then commit + unmount (core::ops::runApplyJob) on the engine thread; live
// progress into AppState::applyRun; cancel between steps. After a committed run the edition is
// re-read from the WIM ("Sonra" size) and the source is refreshed.
#include "app/state/AppState.h"

#include <functional>
#include <memory>

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
    void cancel();

private:
    AppState& m_state;
    Events m_events;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace wl::app
