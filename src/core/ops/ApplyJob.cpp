#include "core/ops/ApplyJob.h"

#include "core/image/dism/DismExe.h"

#include "base/Log.h"
#include "core/image/dism/MountHealth.h"
#include "core/image/wim/WimGapi.h"

#include <algorithm>
#include <format>

namespace wl::core::ops {

namespace {
using Clock = std::chrono::steady_clock;

std::chrono::milliseconds since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
}
} // namespace

namespace {

// A language pack or the display language went in (D-062).
bool languagesChanged(const ApplyReport& report) {
    return std::ranges::any_of(report.results, [](const StepResult& r) {
        const auto& op = r.step.operation;
        return r.outcome.has_value() && ((op.kind == OpKind::AddPackage && op.value == L"language") || op.kind == OpKind::SetIntl);
    });
}

// <setupFolder>\sources\lang.ini from the image's languages; never fatal (the image is fine either way).
bool writeLangIni(DismSession& session, const std::filesystem::path& setupFolder) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(setupFolder / L"sources" / L"lang.ini", ec)) {
        return false; // not a setup folder (a bare WIM): nothing to keep in step
    }
    auto run = runDismExe(session, L"/Gen-LangINI /Distribution:\"" + setupFolder.wstring() + L"\"");
    if (!run || run->exitCode != 0) {
        log::warn("apply", L"lang.ini not written: " + (run ? describe(dismExeFailure(*run, L"dism /Gen-LangINI")) : describe(run.error())));
        return false;
    }
    log::info("apply", L"Setup's language list written: " + (setupFolder / L"sources" / L"lang.ini").wstring());
    return true;
}

} // namespace

Result<ApplyJobResult> runApplyJob(Dism& dism, const std::filesystem::path& mountDir, const ApplyPlan& plan,
                                   const ApplyJobOptions& options, const TaskContext& task,
                                   const ApplyJobCallbacks& callbacks) {
    const auto started = Clock::now();
    ApplyJobResult result;

    // Weights: each step's estimate, then the commit.
    double stepsWeight = 0;
    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        stepsWeight += estimateSeconds(plan, i);
    }
    const double commitWeight = options.commitAndUnmount ? kCommitSeconds : 0.0;
    const double optimizeWeight = options.commitAndUnmount && !options.optimizeWim.empty() ? kOptimizeSeconds : 0.0;
    const double total = std::max(stepsWeight + commitWeight + optimizeWeight, 1.0);

    {
        auto session = dism.openSession(mountDir);
        if (!session) {
            return std::unexpected(session.error());
        }
        std::vector<Clock::time_point> stepStart(plan.steps.size());
        ApplyCallbacks steps = callbacks.steps;
        steps.stepStarted = [&](std::size_t i, const PlanStep& step) {
            stepStart[i] = Clock::now();
            if (callbacks.steps.stepStarted) {
                callbacks.steps.stepStarted(i, step);
            }
        };
        steps.stepFinished = [&](std::size_t i, const StepResult& r) {
            result.stepTimes.push_back(since(stepStart[i]));
            if (callbacks.steps.stepFinished) {
                callbacks.steps.stepFinished(i, r);
            }
        };
        // The applier reports 0…1 over its steps; rescale into the steps' share of the job.
        const TaskContext stepsTask{task.cancel, [&](double fraction, std::wstring_view stage) {
                                        task.report(fraction * stepsWeight / total, stage);
                                    }};
        result.report = apply(plan, **session, stepsTask, ErrorPolicy::Skip, steps, options.apply);
        if (result.report.completed && !options.setupFolder.empty() && languagesChanged(result.report)) {
            result.langIniWritten = writeLangIni(**session, options.setupFolder);
        }
    } // session closed here: DISM refuses to unmount an image with an open session

    if (!result.report.completed) {
        // Cancelled: nothing more; the image stays mounted with whatever ran.
        result.elapsed = since(started);
        return result;
    }
    if (options.commitAndUnmount) {
        if (callbacks.committing) {
            callbacks.committing();
        }
        const auto commitStart = Clock::now();
        const TaskContext commitTask{task.cancel, [&](double fraction, std::wstring_view stage) {
                                         task.report((stepsWeight + fraction * commitWeight) / total, stage);
                                     }};
        if (auto r = unmountSafely(dism, mountDir, /*commit=*/true, commitTask); !r) {
            log::error("apply", L"commit failed: " + describe(r.error()));
            result.commitError = r.error();
        } else {
            result.committed = true;
            const bool editionChanged = std::ranges::any_of(result.report.results, [](const StepResult& r) {
                return r.step.operation.kind == OpKind::SetEdition && r.outcome.has_value();
            });
            if (options.editionTexts && editionChanged) {
                // Never fatal: the image is the new edition either way, only listed under its old name.
                const auto& texts = *options.editionTexts;
                if (auto renamed = setImageText(texts.wim, texts.index, texts.text); !renamed) {
                    log::warn("apply", L"edition changed, but the image keeps its old name: " + describe(renamed.error()));
                } else {
                    result.editionRenamed = true;
                }
            }
            if (!options.optimizeWim.empty()) {
                // Not cancellable and never fatal: the image is saved either way.
                const TaskContext optimizeTask{CancelToken{}, [&](double fraction, std::wstring_view stage) {
                                                   task.report((stepsWeight + commitWeight + fraction * optimizeWeight) / total,
                                                               stage);
                                               }};
                if (auto rewritten = optimizeWim(options.optimizeWim, optimizeTask); !rewritten) {
                    log::warn("apply", L"image saved, but not rewritten without the commit's leftovers: " +
                                           describe(rewritten.error()));
                } else {
                    result.optimized = true;
                }
            }
        }
        result.commitTime = since(commitStart);
    }
    result.elapsed = since(started);
    task.report(1.0, L"done");
    log::info("apply", std::format(L"apply finished: {} step(s), {} failed, commit {} ({} ms)", result.report.results.size(),
                                   result.report.failures(), result.committed ? L"ok" : L"no", result.elapsed.count()));
    return result;
}

ApplyPlan planForOtherEdition(const ApplyPlan& plan) {
    ApplyPlan other;
    other.warnings = plan.warnings;
    for (const auto& step : plan.steps) {
        if (step.operation.kind != OpKind::SetEdition) {
            other.steps.push_back(step);
        }
    }
    return other;
}

Result<ApplyJobResult> applyToEdition(Dism& dism, const std::filesystem::path& wim, int index,
                                      const std::filesystem::path& mountDir, const ApplyPlan& plan,
                                      const ApplyJobOptions& options, const TaskContext& task,
                                      const ApplyJobCallbacks& callbacks) {
    log::info("apply", std::format(L"next edition: {} [{}]", wim.wstring(), index));
    // Mount ~15 %, the rest is the job (its own steps + commit).
    constexpr double kMountShare = 0.15;
    const TaskContext mountTask{task.cancel, [&](double f, std::wstring_view stage) { task.report(f * kMountShare, stage); }};
    if (auto mounted = dism.mount(wim, index, mountDir, /*readOnly=*/false, mountTask); !mounted) {
        return std::unexpected(mounted.error());
    }
    ApplyJobOptions own = options;
    own.commitAndUnmount = true;
    own.optimizeWim.clear();  // once, after the last edition
    own.editionTexts.reset(); // no edition change on these
    const TaskContext jobTask{task.cancel, [&](double f, std::wstring_view stage) {
                                  task.report(kMountShare + f * (1.0 - kMountShare), stage);
                              }};
    auto job = runApplyJob(dism, mountDir, planForOtherEdition(plan), own, jobTask, callbacks);
    // Whatever happened, the folder must be free for the next edition.
    const bool stillMounted = !job || !job->committed;
    if (stillMounted) {
        log::warn("apply", std::format(L"edition [{}] not saved: its changes are discarded", index));
        if (auto discarded = unmountSafely(dism, mountDir, /*commit=*/false, TaskContext{}); !discarded) {
            log::error("apply", L"could not discard the edition: " + describe(discarded.error()));
        }
    }
    return job;
}

} // namespace wl::core::ops
