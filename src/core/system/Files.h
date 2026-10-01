#pragma once
// File-system helpers the engine needs in several places: a reparse-point test, the size of a
// file or folder tree, and replacing a big file through a rename (never a delete first).
#include "base/Result.h"

#include <cstdint>
#include <filesystem>

namespace wl::core {

// The user's temp folder (%TEMP%); scratch files of the engine go under it.
[[nodiscard]] std::filesystem::path tempFolder();

// Any reparse point (junction, symlink, a mounted WIM's own file tags …); missing = false.
[[nodiscard]] bool isReparsePoint(const std::filesystem::path& path);

// Bytes of a file, or of every regular file under a folder. Entries that cannot be read count 0
// (std::filesystem reports their size as uintmax_t(-1)).
[[nodiscard]] std::uint64_t treeBytes(const std::filesystem::path& path);

// `fresh` (complete) takes the place of `original` under the name `destination` (often the same
// path; .wim → .esd changes it). The original is renamed to "<original>.old" first and only
// deleted once `fresh` is in place; on failure it is renamed back and `fresh` is removed.
[[nodiscard]] Result<void> swapIntoPlace(const std::filesystem::path& original, const std::filesystem::path& fresh,
                                         const std::filesystem::path& destination);

} // namespace wl::core
