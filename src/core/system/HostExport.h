#pragma once
// D-052: this PC's third-party drivers, for an image that is to run on the same hardware
// ("Bu bilgisayarın sürücülerini al"): Windows' own
//   pnputil.exe /export-driver * <folder>
// copies every oemN.inf package of the driver store (with its catalog and binaries) into a
// subfolder each; the folder is then scanned like any other (DriverInf.h). Elevated process.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <filesystem>

namespace wl::core {

// Returns the number of driver packages (subfolders with an .inf) now in `folder`.
[[nodiscard]] Result<int> exportHostDrivers(const std::filesystem::path& folder, const TaskContext& task);

} // namespace wl::core
