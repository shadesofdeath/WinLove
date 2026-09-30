#pragma once
// Small text files WinLove puts into an image — the Start layout of the default profile is the
// first: Windows 10 takes its tiles from Users\Default\…\Shell\LayoutModification.xml and has no
// registry value for it. In the queue a file is one WriteFile operation (target = the path in the
// image, value = the text), so a preset carries it.
//
// The path is user input (it arrives in preset files). Only the places where per-machine and
// default-profile configuration lives are accepted — Users\Default\… and ProgramData\… — with
// the rules of a component path (relative, no "." / "..", no drive or stream syntax) and never
// through a link.
#include "base/Result.h"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace wl::core {

inline constexpr std::size_t kImageFileLimit = 1u << 20; // 1 MiB: these are configuration files

[[nodiscard]] Result<void> validateImageFile(std::wstring_view relative, std::size_t bytes);

// Writes `content` as it is (UTF-8 from the caller) to <mountDir>\<relative>, replacing a file
// that is there; missing folders below the accepted root are created. The root itself
// (Users\Default, ProgramData) must exist: a folder that is not a Windows image gets nothing.
[[nodiscard]] Result<void> writeImageFile(const std::filesystem::path& mountDir, std::wstring_view relative,
                                          std::string_view content);

} // namespace wl::core
