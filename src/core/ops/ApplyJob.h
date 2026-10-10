#pragma once
// The whole "Uygula" run on the engine thread (P05): open a DISM session on the mounted image,
// run the plan (errors are skipped and reported, never silently), close the session, then
// commit + unmount (MountHealth::unmountSafely) — unless a step failed: then the image waits,
// mounted, for the user (ApplyJobOptions::commitWithFailures). Progress: the plan's steps and the commit share
// one 0…1 scale, weighted by their time estimates.
#include "core/image/wim/WimGapi.h"
#include "core/ops/Applier.h"

#include <chrono>

namespace wl::core::ops {

struct ApplyJobOptions {
    ApplyOptions apply;
    bool commitAndUnmount = true; // false: leave the image mounted (changes stay in the mount)
    // A failed step may have left the image half changed (a component's packages unlocked, hives
    // edited, files gone): then the image is not saved but stays mounted for the user to decide
    // (ApplyJobResult::held, audit A7). True keeps the old way: save whatever ran.
    bool commitWithFailures = false;
    // After a commit: rewrite this WIM without what the commit orphaned (optimizeWim). Empty: no.
    std::filesystem::path optimizeWim;
    // After a commit in which the edition change succeeded: the texts of that edition in the WIM
    // (Edition.h textAfterEditionChange). The commit itself only records the new edition id.
    struct EditionTexts {
        std::filesystem::path wim;
        int index = 0;
        ImageText text;
    };
    std::optional<EditionTexts> editionTexts;
    // D-062: the setup folder the image belongs to (<folder>\sources\lang.ini). When the run
    // changed languages, Setup's list of them is written again from the image before the commit
    // (dism /Gen-LangINI), so "Language to install" offers the added ones. Empty: not touched.
    std::filesystem::path setupFolder;
};

struct ApplyJobCallbacks {
    ApplyCallbacks steps;
    std::function<void()> committing; // the steps are done; saving + unmounting starts
    // D-106: the steps are done and the image is about to be saved (nothing failed, nothing held).
    // Called on the engine thread with the DISM session closed: the mount folder is the user's to
    // edit by hand. It returns when the user lets the job go on: true saves, false leaves the
    // image mounted, not saved (ApplyJobResult::kept).
    std::function<bool()> beforeCommit;
};

struct ApplyJobResult {
    ApplyReport report;
    bool committed = false;
    std::optional<Error> commitError; // steps ran but saving failed: the image is still mounted
    bool held = false;                // steps failed: not saved, still mounted (ApplyJobOptions::commitWithFailures)
    bool kept = false;                // ApplyJobCallbacks::beforeCommit said no: not saved, still mounted
    bool optimized = false;           // the WIM was rewritten without the commit's leftovers
    bool editionRenamed = false;      // the WIM names the new edition (ApplyJobOptions::editionTexts)
    bool langIniWritten = false;      // <setupFolder>\sources\lang.ini generated from the image
    std::chrono::milliseconds elapsed{0};
    std::vector<std::chrono::milliseconds> stepTimes; // per plan step
    std::chrono::milliseconds commitTime{0};
};

// Fails only when nothing could start (DISM missing, session refused). Per-step failures are in
// report.results; a failed commit is in commitError.
[[nodiscard]] Result<ApplyJobResult> runApplyJob(Dism& dism, const std::filesystem::path& mountDir,
                                                 const ApplyPlan& plan, const ApplyJobOptions& options,
                                                 const TaskContext& task, const ApplyJobCallbacks& callbacks = {});

// ---- D-055: the same queue on further editions of the WIM ------------------------------------
// After the mounted edition, each further one is mounted (read-write) into the same folder, the
// plan runs on it, and it is committed and unmounted — one after the other. The edition change is
// left out: it belongs to the edition it was queued on. A commit that fails is discarded so the
// next edition can be mounted; the WIM is rewritten (optimizeWim) once, by the caller, at the end.
[[nodiscard]] ApplyPlan planForOtherEdition(const ApplyPlan& plan);
[[nodiscard]] Result<ApplyJobResult> applyToEdition(Dism& dism, const std::filesystem::path& wim, int index,
                                                    const std::filesystem::path& mountDir, const ApplyPlan& plan,
                                                    const ApplyJobOptions& options, const TaskContext& task,
                                                    const ApplyJobCallbacks& callbacks = {});

} // namespace wl::core::ops
