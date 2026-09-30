#pragma once
// Component store cleanup of a mounted image (P07 "Temizlik", D-031): superseded component
// versions leave WinSxS, with /ResetBase for good (installed updates can no longer be
// uninstalled). The DISM API has no call for it, so this runs Windows' own
//   dism.exe /Image:<mount> /Cleanup-Image /StartComponentCleanup [/ResetBase]
// with our session closed meanwhile. Fails with 0x800F0806 when the image has pending servicing
// operations. The Planner runs it right after the updates (Planner.h).
#include "core/image/dism/Dism.h"

#include <optional>
#include <string>
#include <string_view>

namespace wl::core {

struct StoreCleanupOptions {
    std::wstring title; // as the Apply list and presets show it
    bool resetBase = true;

    [[nodiscard]] bool operator==(const StoreCleanupOptions&) const = default;
};
// The value of a CleanupImage operation.
[[nodiscard]] std::string storeCleanupToJson(const StoreCleanupOptions& options);
[[nodiscard]] Result<StoreCleanupOptions> storeCleanupFromJson(std::string_view json);

[[nodiscard]] std::wstring storeCleanupCommandLine(const std::filesystem::path& dismExe,
                                                   const std::filesystem::path& mountDir, bool resetBase);
// The last percentage in a piece of dism.exe console output ("[=====   20.0%   ]"), 0 … 1.
[[nodiscard]] std::optional<double> lastDismPercent(std::string_view output);

// Not cancellable once dism.exe runs: stopping it half way can leave the store inconsistent.
[[nodiscard]] Result<void> cleanupComponentStore(DismSession& session, bool resetBase, const TaskContext& task);

} // namespace wl::core
