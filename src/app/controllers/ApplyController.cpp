#include "app/controllers/ApplyController.h"

#include "core/image/dism/Edition.h"

#include "base/Log.h"
#include "core/image/dism/Dism.h"
#include "core/system/Privileges.h"
#include "ui/anim/Tween.h"

#include <algorithm>
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

std::vector<ApplyController::Edition> ApplyController::otherEditions() const {
    std::vector<Edition> list;
    const auto& mounted = m_state.mounted();
    const auto& source = m_state.source();
    if (!mounted || !source || mounted->readOnly) {
        return list;
    }
    for (const auto& image : source->install.images) {
        if (image.index != mounted->index) {
            list.push_back({image.index, image.name});
        }
    }
    return list;
}

void ApplyController::setExtraEdition(int index, bool on) {
    std::erase(m_extra, index);
    if (on) {
        m_extra.push_back(index);
        std::ranges::sort(m_extra);
    }
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
    for (const auto& e : otherEditions()) {
        if (std::ranges::find(m_extra, e.index) != m_extra.end()) {
            run.extras.push_back({e.index, e.name});
        }
    }
    m_extra.clear(); // one run's choice
    if (const auto& source = m_state.source()) {
        for (const auto& image : source->install.images) {
            if (image.index == mounted.index) {
                run.sizeBefore = image.totalBytes;
            }
        }
    }
    // Payload for NetFx3 / removed features: the setup media's sources\sxs next to the WIM.
    core::ops::ApplyJobOptions options;
    // No "[DELETED]" leftovers of the commit in the file — once, after the last edition.
    options.optimizeWim = run.extras.empty() ? mounted.imagePath : std::filesystem::path();
    if (const auto* edition = run.changes.find(core::ops::OpKind::SetEdition, L"edition")) {
        // The commit records the new edition id; the name the edition is listed under is ours to set.
        if (const auto& source = m_state.source()) {
            for (const auto& image : source->install.images) {
                if (image.index == mounted.index) {
                    options.editionTexts = core::ops::ApplyJobOptions::EditionTexts{
                        mounted.imagePath, mounted.index, core::textAfterEditionChange(image, edition->value)};
                }
            }
        }
    }
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
        std::vector<AppState::ApplyRun::ExtraEdition> extras;
    };
    auto extras = m_state.applyRun()->extras;
    const std::filesystem::path sourcePath = m_state.source() ? m_state.source()->path : mounted.imagePath;
    m_state.engine().run<Outcome>(
        [mounted, plan, options, cancel, update, lastPercent, callbacks, sourcePath, extras](
            const core::TaskContext&) mutable -> Result<Outcome> {
            auto dism = core::Dism::instance();
            if (!dism) {
                return std::unexpected(dism.error());
            }
            // With further editions each one is a share of the bar.
            const double editions = 1.0 + static_cast<double>(extras.size());
            auto progress = [update, lastPercent, editions](double base) {
                return [update, lastPercent, editions, base](double fraction, std::wstring_view) {
                    const double overall = (base + std::clamp(fraction, 0.0, 1.0)) / editions;
                    const int percent = static_cast<int>(std::floor(overall * 100));
                    if (lastPercent->exchange(percent) != percent) {
                        update([overall](Run& r) { r.fraction = overall; });
                    }
                };
            };
            const core::TaskContext task{cancel, progress(0.0)};
            auto job = core::ops::runApplyJob(**dism, mounted.mountDir, plan, options, task, callbacks);
            if (!job) {
                return std::unexpected(job.error());
            }
            Outcome outcome{std::move(*job), std::nullopt, {}};
            // D-055: the other editions, only after the mounted one was saved.
            if (outcome.job.committed && !extras.empty()) {
                for (std::size_t i = 0; i < extras.size(); ++i) {
                    auto& extra = extras[i];
                    if (cancel.cancelled()) {
                        extra.error = L"cancelled";
                        extra.state = AppState::ApplyRun::ExtraEdition::State::Failed;
                        continue;
                    }
                    update([i](Run& r) {
                        r.extraCurrent = static_cast<int>(i);
                        r.extras[i].state = AppState::ApplyRun::ExtraEdition::State::Running;
                        r.stage = Run::Stage::Running;
                        std::ranges::fill(r.stepState, 0);
                        r.currentStep = -1;
                    });
                    // Step indexes of the edition plan (no edition change) map back onto the run's list.
                    std::vector<std::size_t> toRun;
                    for (std::size_t s = 0; s < plan.steps.size(); ++s) {
                        if (plan.steps[s].operation.kind != core::ops::OpKind::SetEdition) {
                            toRun.push_back(s);
                        }
                    }
                    core::ops::ApplyJobCallbacks own;
                    own.steps.stepStarted = [update, toRun](std::size_t k, const core::ops::PlanStep&) {
                        const std::size_t s = toRun[k];
                        update([s](Run& r) {
                            r.currentStep = static_cast<int>(s);
                            r.stepState[s] = 1;
                        });
                    };
                    own.steps.stepFinished = [update, toRun](std::size_t k, const core::ops::StepResult& result) {
                        const std::size_t s = toRun[k];
                        const bool ok = result.outcome.has_value();
                        update([s, ok](Run& r) { r.stepState[s] = ok ? 2 : 3; });
                    };
                    own.committing = [update] { update([](Run& r) { r.stage = Run::Stage::Committing; }); };
                    const core::TaskContext editionTask{cancel, progress(1.0 + static_cast<double>(i))};
                    auto other = core::ops::applyToEdition(**dism, mounted.imagePath, extra.index, mounted.mountDir, plan,
                                                           options, editionTask, own);
                    if (!other) {
                        extra.state = AppState::ApplyRun::ExtraEdition::State::Failed;
                        extra.error = other.error().message;
                    } else if (!other->committed) {
                        extra.state = AppState::ApplyRun::ExtraEdition::State::Failed;
                        // No message of its own: the page says "not saved" (apply.editionFailed).
                        extra.error = other->commitError ? other->commitError->message : std::wstring();
                        extra.failures = other->report.failures();
                    } else {
                        extra.state = AppState::ApplyRun::ExtraEdition::State::Done;
                        extra.failures = other->report.failures();
                    }
                    const auto copy = extra;
                    update([i, copy](Run& r) { r.extras[i] = copy; });
                }
                // Once, for all the commits.
                if (auto rewritten = core::optimizeWim(mounted.imagePath, core::TaskContext{}); !rewritten) {
                    log::warn("apply", L"editions saved, but the WIM was not rewritten: " + describe(rewritten.error()));
                } else {
                    outcome.job.optimized = true;
                }
            }
            outcome.extras = std::move(extras);
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
                // The image is still mounted but may have changed: the cached lists (features,
                // apps, services) no longer describe it, so pages must read them again.
                auto invalidateLists = [this] {
                    m_state.setOptionalFeatures(std::nullopt);
                    m_state.setAppxList(std::nullopt);
                    m_state.setServiceList(std::nullopt);
                };
                if (!result) {
                    invalidateLists();
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
                if (!outcome.extras.empty()) {
                    run->extras = std::move(outcome.extras);
                }
                run->extraCurrent = -1;
                const bool committed = run->result->committed;
                m_state.notifyApply();
                if (!committed && std::ranges::any_of(run->result->report.results,
                                                      [](const auto& r) { return r.outcome.has_value(); })) {
                    invalidateLists();
                }
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
