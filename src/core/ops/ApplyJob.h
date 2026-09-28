#pragma once
// The whole "Uygula" run on the engine thread (P05): open a DISM session on the mounted image,
// run the plan (errors are skipped and reported, never silently), close the session, then
// commit + unmount (MountHealth::unmountSafely). Progress: the plan's steps and the commit share
// one 0…1 scale, weighted by their time estimates.
#include "core/ops/Applier.h"

#include <chrono>

namespace wl::core::ops {

struct ApplyJobOptions {
    ApplyOptions apply;
    bool commitAndUnmount = true; // false: leave the image mounted (changes stay in the mount)
};

struct ApplyJobCallbacks {
    ApplyCallbacks steps;
    std::function<void()> committing; // the steps are done; saving + unmounting starts
};

struct ApplyJobResult {
    ApplyReport report;
    bool committed = false;
    std::optional<Error> commitError; // steps ran but saving failed: the image is still mounted
    std::chrono::milliseconds elapsed{0};
    std::vector<std::chrono::milliseconds> stepTimes; // per plan step
    std::chrono::milliseconds commitTime{0};
};

// Fails only when nothing could start (DISM missing, session refused). Per-step failures are in
// report.results; a failed commit is in commitError.
[[nodiscard]] Result<ApplyJobResult> runApplyJob(Dism& dism, const std::filesystem::path& mountDir,
                                                 const ApplyPlan& plan, const ApplyJobOptions& options,
                                                 const TaskContext& task, const ApplyJobCallbacks& callbacks = {});

} // namespace wl::core::ops
