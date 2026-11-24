#pragma once
// Runs a Windows tool hidden and hands everything it prints to the caller: dism.exe (DismExe.h),
// diskpart.exe and bootsect.exe (USB media, D-047).
#include "base/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace wl::core {

class CancelToken; // core/tasks/Task.h

// What happens to the tool when WinLove dies while it runs.
enum class ProcessLife : std::uint8_t {
    // In a job that ends the tool and what it started with WinLove (D-130): a tool cut short
    // costs nothing (an expand, an export, a query), one left behind holds files or hives.
    EndsWithUs,
    // No job: the tool finishes on its own, as before D-130. For tools that service an image or
    // write a disk (dism.exe on a mounted image, diskpart, bootsect): killing one in the middle
    // of /Add-Package, /Set-Edition, /Cleanup-Image or a format is worse than letting it end.
    Outlives,
};

// The exit code; an error only when the process could not be started. The tool inherits nothing
// but its output pipe; `life` says whether it ends with WinLove (above).
// `cancel` is for tools that are harmless to cut short (an expand, an export) and for nothing
// else: once it is cancelled the tool and everything it started are terminated and the result
// is ErrorCode::Cancelled. Never hand it to a tool that services an image or writes a disk: with
// ProcessLife::Outlives a token is a programming error and is ignored (the tool is never cut).
[[nodiscard]] Result<std::uint32_t> runProcess(std::wstring commandLine,
                                               const std::function<void(std::string_view)>& onOutput,
                                               const CancelToken* cancel = nullptr,
                                               ProcessLife life = ProcessLife::EndsWithUs);

// <System32>\<name>
[[nodiscard]] Result<std::wstring> systemTool(std::wstring_view name);

} // namespace wl::core
