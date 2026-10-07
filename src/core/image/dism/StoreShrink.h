#pragma once
// D-079: the component store at its smallest (what tiny11's "core" builds do). WinSxS keeps only
// what a running Windows loads from it — the Win32 side-by-side assemblies old programs ask for
// (common controls, GDI+, the VC80 / VC90 runtimes, isolation automation) with their redirection
// policies, the servicing stack, and the store's own folders (Manifests, Catalogs, FileMaps,
// Fusion, InstallTemp, SettingsManifests, Temp). Every other component folder goes.
//
// Most of WinSxS is hard links to System32 & co.: deleting those names frees nothing, the other
// name stays. What is freed is what no other folder links to — on 25H2 about 3 GB of 10 GB.
//
// Irreversible and the image can no longer be serviced: no cumulative update, optional feature,
// language or .NET 3.5 can be added afterwards, sfc / DISM RestoreHealth find it corrupt. So it is
// the last step of an Apply (Planner Phase::Shrink), after the store cleanup.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

// Does the shrunk store keep this top-level WinSxS entry? (A folder name, any case.)
[[nodiscard]] bool keptInShrunkStore(std::wstring_view entry);

struct StoreShrinkPlan {
    std::vector<std::wstring> removed; // top-level folder names
    std::size_t kept = 0;
    std::uint64_t files = 0;           // in the removed folders
    std::uint64_t bytes = 0;           // their sizes, as Explorer counts them
    std::uint64_t freed = 0;           // of these, linked nowhere else: what removing them frees
};

// What shrinking <mountDir>\Windows\WinSxS would remove (nothing is changed). `measure` also
// reads every file's link count (~20 s on 25H2); without it only the names are listed.
[[nodiscard]] Result<StoreShrinkPlan> planStoreShrink(const std::filesystem::path& mountDir, bool measure,
                                                      const TaskContext& task);

// Removes them (elevated: SeBackup / SeRestore). Cancellable between folders — a half-shrunk
// store is as unserviceable as a shrunk one, so the caller discards the image on cancel.
// Returns the plan that was carried out, measured.
[[nodiscard]] Result<StoreShrinkPlan> shrinkComponentStore(const std::filesystem::path& mountDir, const TaskContext& task);

// The value of a ShrinkStore operation.
struct StoreShrinkOptions {
    std::wstring title; // as the Apply list and presets show it

    [[nodiscard]] bool operator==(const StoreShrinkOptions&) const = default;
};
[[nodiscard]] std::string storeShrinkToJson(const StoreShrinkOptions& options);
[[nodiscard]] Result<StoreShrinkOptions> storeShrinkFromJson(std::string_view json);

} // namespace wl::core
