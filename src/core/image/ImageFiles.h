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
#include <cstdint>
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

// Is <mountDir>\<relative> there with exactly `content` (what writeImageFile would leave)?
// Missing or different → false; errors only for a path the rules above refuse.
[[nodiscard]] Result<bool> imageFileHas(const std::filesystem::path& mountDir, std::wstring_view relative,
                                        std::string_view content);

// A file of this PC copied into the image (a CopyFile operation: target = the path in the image,
// value = the source here) — the default wallpaper, the lock screen picture. Same places and
// rules as writeImageFile; the source must be a regular file of at most kImageCopyLimit bytes.
inline constexpr std::uint64_t kImageCopyLimit = 64ull << 20;
[[nodiscard]] Result<void> copyImageFile(const std::filesystem::path& mountDir, std::wstring_view relative,
                                         const std::filesystem::path& source);

} // namespace wl::core
