#include "app/controllers/ApplyController.h"

#include "base/Log.h"
#include "core/image/dism/Dism.h"
#include "core/system/Privileges.h"
#include "ui/anim/Tween.h"

#include <atomic>
#include <cmath>
#include <format>

namespace wl::app {

using core::ops::Operation;
using core::ops::Risk;
using Run = AppState::ApplyRun;

ApplyController::ApplyController(AppState& state, Events events) : m_state(state), m_events(std::move(events)) {}

ApplyController::~ApplyController() {
    *m_alive = false;
}

core::ops::ApplyPlan ApplyController::currentPlan() const {
    return core::ops::plan(m_state.changes());
}

std::vector<Operation> ApplyController::highRisk() const {
    std::vector<Operation> result;
    for (const auto& op : m_state.changes().operations()) {
        if (op.risk == Risk::High) {
            result.push_back(op);
        }
    }
    return result;
}

bool ApplyController::running() const {
    const auto& run = m_state.applyRun();
    return run && run->stage != Run::Stage::Done;
}

bool ApplyController::canStart() const {
    return m_state.mounted() && !m_state.changes().empty() && !running() && !m_state.operation();
}

void ApplyController::start() {
    if (!canStart()) {
        return;
    }
    if (!core::isElevated()) {
        m_events.failed(Error{ErrorCode::AccessDenied, L"DISM needs an elevated (administrator) process", L"apply"});
        return;
    }
    const MountedImage mounted = *m_state.mounted();
    Run run;
    run.changes = m_state.changes();
    run.plan = core::ops::plan(run.changes);
    run.groups = core::ops::groups(run.plan);
    run.stepState.assign(run.plan.steps.size(), 0);
    run.startedMs = ui::nowMs();
    run.logVersion = m_state.logBuffer() ? m_state.logBuffer()->version() : 0;
    run.edition = mounted.edition;
    if (const auto& source = m_state.source()) {
        for (const auto& image : source->install.images) {
            if (image.index == mounted.index) {
                run.sizeBefore = image.totalBytes;
            }
        }
    }
    // Payload for NetFx3 / removed features: the setup media's sources\sxs next to the WIM.
    core::ops::ApplyJobOptions options;
    std::error_code ec;
    const auto sxs = mounted.imagePath.parent_path() / L"sxs";
    if (std::filesystem::is_directory(sxs, ec)) {
        options.apply.featureSources.push_back(sxs);
    }
    const core::CancelToken cancel = run.cancel;
    const core::ops::ApplyPlan plan = run.plan;
    m_state.setApplyRun(std::move(run));
    log::info("apply", std::format(L"apply started: {} change(s) on {} [{}]", plan.steps.size(),
                                   mounted.imagePath.wstring(), mounted.index));

    auto post = m_events.postToUi;
    std::weak_ptr<bool> alive = m_alive;
    // Engine → UI: step updates are posted as they happen; progress only when the percent changes.
    auto update = [post, alive, this](std::function<void(Run&)> change) {
        post([alive, this, change = std::move(change)] {
            if (const auto a = alive.lock(); !a || !*a) {
                return;
            }
            if (auto& run = m_state.applyRunMutable()) {
                change(*run);
                m_state.notifyApply();
            }
        });
    };
    auto lastPercent = std::make_shared<std::atomic<int>>(-1);
    core::ops::ApplyJobCallbacks callbacks;
    callbacks.steps.stepStarted = [update](std::size_t i, const core::ops::PlanStep&) {
        update([i](Run& r) {
            r.currentStep = static_cast<int>(i);
            r.stepState[i] = 1;
        });
    };
    callbacks.steps.stepFinished = [update](std::size_t i, const core::ops::StepResult& result) {
        const bool ok = result.outcome.has_value();
        update([i, ok](Run& r) { r.stepState[i] = ok ? 2 : 3; });
    };
    callbacks.committing = [update] { update([](Run& r) { r.stage = Run::Stage::Committing; }); };

    struct Outcome {
        core::ops::ApplyJobResult job;
        std::optional<core::SourceInfo> source; // re-read after commit
    };
    const std::filesystem::path sourcePath = m_state.source() ? m_state.source()->path : mounted.imagePath;
    m_state.engine().run<Outcome>(
        [mounted, plan, options, cancel, update, lastPercent, callbacks, sourcePath](
            const core::TaskContext&) -> Result<Outcome> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            const core::TaskContext task{cancel, [update, lastPercent](double fraction, std::wstring_view) {
                                             const int percent = static_cast<int>(std::floor(fraction * 100));
                                             if (lastPercent->exchange(percent) != percent) {
                                                 update([fraction](Run& r) { r.fraction = fraction; });
                                             }
                                         }};
            auto job = core::ops::runApplyJob(**dism, mounted.mountDir, plan, options, task, callbacks);
            if (!job) {
                return std::unexpected(job.error());
            }
            Outcome outcome{std::move(*job), std::nullopt};
            if (outcome.job.committed) {
                if (auto info = core::openSource(sourcePath)) {
                    outcome.source = std::move(*info);
                }
            }
            return outcome;
        },
        [this, post, alive, mounted](Result<Outcome> result) {
            post([this, alive, mounted, result = std::move(result)]() mutable {
                if (const auto a = alive.lock(); !a || !*a) {
                    return;
                }
                auto& run = m_state.applyRunMutable();
                if (!run) {
                    return;
                }
                run->stage = Run::Stage::Done;
                run->fraction = 1.0;
                if (!result) {
                    log::error("apply", describe(result.error()));
                    run->error = result.error();
                    m_state.notifyApply();
                    m_events.failed(result.error());
                    return;
                }
                auto& outcome = *result;
                // Steps that ran are in the image now: take them out of the queue.
                for (const auto& r : outcome.job.report.results) {
                    if (r.outcome) {
                        m_state.unqueue(r.step.operation.kind, r.step.operation.target);
                    }
                }
                if (outcome.source) {
                    for (const auto& image : outcome.source->install.images) {
                        if (image.index == mounted.index) {
                            run->sizeAfter = image.totalBytes;
                        }
                    }
                }
                run->result = std::move(outcome.job);
                const bool committed = run->result->committed;
                m_state.notifyApply();
                if (committed) {
                    m_state.setMounted(std::nullopt); // saved and unmounted (also clears the rest of the queue)
                    if (outcome.source && m_events.sourceChanged) {
                        m_events.sourceChanged(std::move(*outcome.source));
                    }
                }
                if (m_events.finished) {
                    m_events.finished();
                }
            });
        },
        {});
}

void ApplyController::cancel() {
    if (auto& run = m_state.applyRunMutable(); run && run->stage == Run::Stage::Running) {
        run->cancel.cancel();
        log::warn("apply", L"stop requested: the current DISM call is cancelled, the rest is skipped");
    }
}

} // namespace wl::app
