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
#include "core/tasks/Task.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace wl::core {

inline constexpr std::size_t kImageFileLimit = 1u << 20; // 1 MiB: these are configuration files

// `bytes` at <mountDir>\<relative> as a NEW file. What is there is unlinked first: many of
// Windows' own files are hard links into WinSxS, and writing through one would change the
// component store's copy too (DISM / sfc then report it corrupt). Writes whatever the folder's
// ACL (TrustedInstaller) says, through SeRestorePrivilege + backup semantics — so only for paths
// WinLove itself names, never for user input (writeImageFile / copyImageFile check those first).
// The folder must exist; the new file inherits its ACL.
[[nodiscard]] Result<void> replaceImageFile(const std::filesystem::path& mountDir, std::wstring_view relative,
                                            std::string_view bytes);
// The same with the content of `source` (any size, streamed) and the file attributes given
// (Winre.wim is hidden + system). For files WinLove itself names (D-080: the serviced WinRE).
[[nodiscard]] Result<void> replaceImageFileFrom(const std::filesystem::path& mountDir, std::wstring_view relative,
                                                const std::filesystem::path& source, std::uint32_t attributes,
                                                const TaskContext& task);
// Removes <mountDir>\<relative> (only this name: a hard link's other names stay). Missing = success.
[[nodiscard]] Result<void> unlinkImageFile(const std::filesystem::path& mountDir, std::wstring_view relative);

[[nodiscard]] Result<void> validateImageFile(std::wstring_view relative, std::size_t bytes);
// A WriteFile operation's value as the file's bytes: UTF-8 text, or "base64:…" for a binary file
// (D-069: the empty Start layout).
[[nodiscard]] std::string imageFileBytes(std::wstring_view value);

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
// Bytes WinLove made from a file of this PC (D-105: an icon completed to every size), under the
// rules and the limit of copyImageFile.
[[nodiscard]] Result<void> writeImageCopy(const std::filesystem::path& mountDir, std::wstring_view relative,
                                          std::string_view bytes);

// ---- D-051: files and folders of this PC anywhere in the image (the "Dosyalar" page) --------
// A CopyTree operation: target = where the item ends up in the image, from its root
// ("Tools\Sysinternals", "Users\Public\Desktop\readme.txt"), value = the file or folder here.
// Allowed anywhere a component path is (relative, no "." / "..", no drive or stream syntax,
// never through a link), except what Windows must own alone: the registry hives (config), the
// component store (WinSxS, servicing), WindowsApps, Boot, System Volume Information,
// $Recycle.Bin. Under Windows\ it is allowed but risky (treeTargetRisky).
[[nodiscard]] Result<void> validateTreeTarget(std::wstring_view relative);
[[nodiscard]] bool treeTargetRisky(std::wstring_view relative);
// Bytes under `source` (a file: its size).
[[nodiscard]] std::uint64_t treeSize(const std::filesystem::path& source);
// Copies `source` to <mountDir>\<relative> (a folder: its whole content, merged into what is
// there, files replaced). Progress by bytes; cancellable between files.
[[nodiscard]] Result<std::uint64_t> copyImageTree(const std::filesystem::path& mountDir, std::wstring_view relative,
                                                  const std::filesystem::path& source, const TaskContext& task);

} // namespace wl::core
