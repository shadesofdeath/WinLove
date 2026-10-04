#pragma once
// D-061: one language file into a mounted image, whatever naming it came with (LanguagePacks.h).
//   - LoF-named cab: DismAddPackage on the file, its folder is the source of the dependencies.
//   - UUP-named cab: copied under its LoF name into a scratch folder first — DISM finds the
//     dependencies of a capability only by that name (0x800F0912 otherwise).
//   - UUP language pack (.esd): its single image is applied to a scratch folder, which DISM takes
//     as the expanded package.
// The scratch folder (%TEMP%\WinLove\lang\…) is removed afterwards. Needs the elevated DISM
// session of an apply.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <filesystem>

namespace wl::core {

class DismSession;

[[nodiscard]] Result<void> addLanguagePackage(DismSession& session, const std::filesystem::path& file, const TaskContext& task);

} // namespace wl::core
