#pragma once
// D-138: what an Apply run and the plan before it come to, in the numbers the Apply page and the image
// overview (Görüntü › Özet) both show — the outcome in one word, the steps that ran / were skipped /
// wait, the time taken or left, the size before → after; per plan group what it does to the size and
// how many of its steps are high risk. Pure functions of the run / plan (tests check them without a
// window); the pages call them when the state changes, never while painting.
#include "app/state/AppState.h"

#include <cstdint>
#include <vector>

namespace wl::app {

struct RunSummary {
    // Running / Paused / Committing: the run is on. The rest are how a finished one ended, in the order
    // the Apply page names them: it could not start; saving failed; stopped before the end; a step
    // failed and nothing was saved (A7); stopped at the pause, unsaved (D-106); saved with skipped
    // steps; saved, every step done.
    enum class Outcome : std::uint8_t { Running, Paused, Committing, StartFailed, CommitFailed, Stopped, Held, Kept, Skipped, Succeeded };
    Outcome outcome = Outcome::Running;
    std::size_t total = 0;   // plan steps
    std::size_t ok = 0;      // ran without an error
    std::size_t skipped = 0; // failed and passed over (the run went on)
    std::size_t pending = 0; // not run: not yet (running) or not at all (stopped)
    int current = -1;        // the running step, -1 when none
    double fraction = 0;     // 0..1
    double elapsedSeconds = 0;
    double remainingSeconds = -1; // < 0: not known (yet)
    std::uint64_t sizeBefore = 0; // expanded bytes of the edition before the run
    std::uint64_t sizeAfter = 0;  // re-read after the save; 0 = not known
    std::uint64_t gain = 0;       // before − after when it shrank
    int gainPct = 0;              // of before
    std::size_t editions = 1;     // D-055: the mounted one and the further ones
    std::size_t editionsDone = 0; // further editions saved (failures counted in their own line)
    // 2026-11-11: what became of the image the run left mounted (stopped / held / kept / failed) —
    // a save or a discard started anywhere (AppState::ApplyRun::closing).
    enum class Close : std::uint8_t { None, Saving, Discarding, Saved, Discarded, SaveFailed, DiscardFailed };
    Close close = Close::None;
    // D-144: "Bitince ISO oluştur" — None when the run was not asked for one; else where it stands
    // (the ISO's progress itself is AppState::isoRun's). NotStarted: `isoSkip` says why.
    enum class Iso : std::uint8_t { None, Waiting, Running, Done, Failed, Cancelled, NotStarted };
    Iso iso = Iso::None;
    AppState::ApplyRun::AutoIso::Skip isoSkip = AppState::ApplyRun::AutoIso::Skip::None;

    [[nodiscard]] bool active() const noexcept {
        return outcome == Outcome::Running || outcome == Outcome::Paused || outcome == Outcome::Committing;
    }
    // The image was written: what ran is in the WIM (the next step is the ISO) — by the run's own save
    // or by a save afterwards.
    [[nodiscard]] bool saved() const noexcept {
        return outcome == Outcome::Succeeded || outcome == Outcome::Skipped || close == Close::Saved;
    }
    // D-144: the ISO after the run is being built.
    [[nodiscard]] bool isoRunning() const noexcept { return iso == Iso::Running; }
    // A save / discard of what the run left mounted is going on.
    [[nodiscard]] bool closing() const noexcept { return close == Close::Saving || close == Close::Discarding; }
    // Unmounted after the run without saving: the image is as it was before it.
    [[nodiscard]] bool discarded() const noexcept { return close == Close::Discarded; }
    // Still mounted and unsaved, waiting for the user's word (save or discard).
    [[nodiscard]] bool waiting() const noexcept {
        return (outcome == Outcome::Held || outcome == Outcome::Kept || outcome == Outcome::Stopped) &&
               (close == Close::None || close == Close::SaveFailed || close == Close::DiscardFailed);
    }
};

// `nowMs`: ui::nowMs() — a running run's elapsed and remaining time.
[[nodiscard]] RunSummary summarizeRun(const AppState::ApplyRun& run, double nowMs);

// One line of the plan before a run: a phase group, what its steps do to the image's size (their
// estimates summed: removals negative) and how many of them are high risk.
struct PlanRow {
    core::ops::PlanGroup group;
    std::int64_t sizeDelta = 0;
    std::size_t risky = 0;
};
[[nodiscard]] std::vector<PlanRow> planRows(const core::ops::ApplyPlan& plan);
// The whole run's estimate: its groups, then saving the image and rewriting the WIM.
[[nodiscard]] double planSeconds(const std::vector<core::ops::PlanGroup>& groups);

} // namespace wl::app
