#pragma once
// Runs a Windows tool hidden and hands everything it prints to the caller: dism.exe (DismExe.h),
// diskpart.exe and bootsect.exe (USB media, D-047).
#include "base/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace wl::core {

// The exit code; an error only when the process could not be started.
[[nodiscard]] Result<std::uint32_t> runProcess(std::wstring commandLine,
                                               const std::function<void(std::string_view)>& onOutput);

// <System32>\<name>
[[nodiscard]] Result<std::wstring> systemTool(std::wstring_view name);

} // namespace wl::core
