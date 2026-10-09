#pragma once
// The work copy of an ISO: DISM and wimgapi need real files, so an ISO source is extracted to
// <workRoot>\work\<ISO name> before it is mounted, edited or rebuilt. A record next to the folder
// (<folder>.source.json) says which ISO it came from, whether the extraction finished, and the size
// and time of its image files then. Opening the original ISO again must neither overwrite a copy
// that was customised since (it did: the extraction "resumed" over every file whose size differed)
// nor mix two ISOs that share a file name (audit A5).
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <filesystem>

namespace wl::core {

enum class WorkCopyState : std::uint8_t {
    Missing,     // no folder, or an empty one: extract
    Partial,     // this ISO's, the extraction did not finish: resume it
    Pristine,    // this ISO's, extracted, its image files as extracted: use as it is
    Modified,    // this ISO's, but its image files changed since (mounted and saved, editions edited)
    Unknown,     // files without a record (builds before 2026-10-09): from this ISO or not, edited or not
    OtherSource, // another ISO's copy (same file name, other file)
};
[[nodiscard]] const wchar_t* workCopyStateName(WorkCopyState state) noexcept;

// Where the record of `folder` is kept: next to it, never inside (the folder becomes the ISO).
[[nodiscard]] std::filesystem::path workCopyRecord(const std::filesystem::path& folder);

[[nodiscard]] WorkCopyState inspectWorkCopy(const std::filesystem::path& iso, const std::filesystem::path& folder);

// Extracts `iso` into `folder` and records it. `fresh` empties the folder first (whatever was in
// it is gone); otherwise files already there are kept as they are and only missing ones are
// copied (a resumed extraction never rewrites a file that is complete).
[[nodiscard]] Result<void> extractWorkCopy(const std::filesystem::path& iso, const std::filesystem::path& folder,
                                           bool fresh, const TaskContext& task);

} // namespace wl::core
