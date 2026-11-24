#include "app/pages/apply/RunSummary.h"

#include <algorithm>
#include <cmath>

namespace wl::app {

using Run = AppState::ApplyRun;

RunSummary summarizeRun(const Run& run, double nowMs) {
    RunSummary s;
    s.total = run.plan.steps.size();
    s.fraction = std::clamp(run.fraction, 0.0, 1.0);
    s.sizeBefore = run.sizeBefore;
    s.sizeAfter = run.sizeAfter;
    s.editions = run.extras.size() + 1;
    s.editionsDone = static_cast<std::size_t>(std::ranges::count_if(
        run.extras, [](const Run::ExtraEdition& e) { return e.state == Run::ExtraEdition::State::Done; }));
    if (s.sizeAfter > 0 && s.sizeBefore > s.sizeAfter) {
        s.gain = s.sizeBefore - s.sizeAfter;
        s.gainPct = static_cast<int>(std::lround(100.0 * static_cast<double>(s.gain) / static_cast<double>(s.sizeBefore)));
    }

    if (run.result) {
        // What the run says, step by step: the report is the truth once it is there.
        const auto& results = run.result->report.results;
        for (const auto& r : results) {
            (r.outcome ? s.ok : s.skipped) += 1;
        }
        s.pending = s.total > results.size() ? s.total - results.size() : 0;
    } else {
        for (std::size_t i = 0; i < run.stepState.size(); ++i) {
            const int state = run.stepState[i];
            if (state == 2) {
                ++s.ok;
            } else if (state == 3) {
                ++s.skipped;
            } else {
                ++s.pending;
                if (state == 1 && s.current < 0) {
                    s.current = static_cast<int>(i);
                }
            }
        }
        // A plan longer than the states (should not happen): the rest waits.
        if (run.stepState.size() < s.total) {
            s.pending += s.total - run.stepState.size();
        }
    }

    switch (run.stage) {
    case Run::Stage::Running: s.outcome = RunSummary::Outcome::Running; break;
    case Run::Stage::Paused: s.outcome = RunSummary::Outcome::Paused; break;
    case Run::Stage::Committing: s.outcome = RunSummary::Outcome::Committing; break;
    case Run::Stage::Done:
        if (run.error || !run.result) {
            s.outcome = RunSummary::Outcome::StartFailed;
        } else if (run.result->commitError) {
            s.outcome = RunSummary::Outcome::CommitFailed;
        } else if (!run.result->report.completed) {
            s.outcome = RunSummary::Outcome::Stopped;
        } else if (run.result->held) {
            s.outcome = RunSummary::Outcome::Held;
        } else if (run.result->kept) {
            s.outcome = RunSummary::Outcome::Kept;
        } else if (s.skipped > 0) {
            s.outcome = RunSummary::Outcome::Skipped;
        } else {
            s.outcome = RunSummary::Outcome::Succeeded;
        }
        break;
    }

    if (run.closing) {
        using State = Run::Closing::State;
        const bool commit = run.closing->commit;
        switch (run.closing->state) {
        case State::Running: s.close = commit ? RunSummary::Close::Saving : RunSummary::Close::Discarding; break;
        case State::Done: s.close = commit ? RunSummary::Close::Saved : RunSummary::Close::Discarded; break;
        case State::Failed: s.close = commit ? RunSummary::Close::SaveFailed : RunSummary::Close::DiscardFailed; break;
        }
    }

    if (run.autoIso) {
        using State = Run::AutoIso::State;
        switch (run.autoIso->state) {
        case State::Waiting: s.iso = RunSummary::Iso::Waiting; break;
        case State::Running: s.iso = RunSummary::Iso::Running; break;
        case State::Done: s.iso = RunSummary::Iso::Done; break;
        case State::Failed: s.iso = RunSummary::Iso::Failed; break;
        case State::Cancelled: s.iso = RunSummary::Iso::Cancelled; break;
        case State::NotStarted: s.iso = RunSummary::Iso::NotStarted; break;
        }
        s.isoSkip = run.autoIso->skip;
    }

    if (run.result) {
        s.elapsedSeconds = static_cast<double>(run.result->elapsed.count()) / 1000.0;
    } else if (s.active() && run.startedMs > 0) {
        s.elapsedSeconds = std::max(0.0, (nowMs - run.startedMs) / 1000.0);
    }
    // The same guess as the page's header: from the pace so far, once there is a pace to go by. Not at a
    // pause: that waits for the user, not for the image.
    if ((s.outcome == RunSummary::Outcome::Running || s.outcome == RunSummary::Outcome::Committing) && s.fraction > 0.03 &&
        s.fraction < 1.0) {
        s.remainingSeconds = s.elapsedSeconds * (1.0 - s.fraction) / s.fraction;
    }
    return s;
}

std::vector<PlanRow> planRows(const core::ops::ApplyPlan& plan) {
    std::vector<PlanRow> rows;
    for (const auto& group : core::ops::groups(plan)) {
        PlanRow row{group};
        for (std::size_t i = group.first; i < group.first + group.count && i < plan.steps.size(); ++i) {
            const auto& op = plan.steps[i].operation;
            row.sizeDelta += op.sizeDelta;
            row.risky += op.risk == core::ops::Risk::High ? 1 : 0;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

double planSeconds(const std::vector<core::ops::PlanGroup>& groups) {
    double seconds = core::ops::kCommitSeconds + core::ops::kOptimizeSeconds;
    for (const auto& g : groups) {
        seconds += g.estimateSeconds;
    }
    return seconds;
}

} // namespace wl::app
