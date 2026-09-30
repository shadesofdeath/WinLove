#pragma once
// WimgApiBackend (docs/ENGINE.md §1): export / delete editions through wimgapi.dll, loaded at
// runtime from System32 like dismapi.dll (the header ships only with the ADK — D-017).
// No admin needed: these write WIM files, they do not mount.
#include "core/image/WimFile.h"
#include "core/tasks/Task.h"

#include <filesystem>

namespace wl::core {

// Copies edition `index` of `source` (WIM or ESD) into `destination`: creates the file if missing,
// otherwise appends as a new index. ESD → WIM = export with Lzx (or Xpress) compression.
[[nodiscard]] Result<void> exportImage(const std::filesystem::path& source, int index,
                                       const std::filesystem::path& destination, WimCompression compression,
                                       const TaskContext& task);

// Rewrites `wim` without the streams no image refers to any more. A commit only appends: the old
// versions of changed files (the registry hives, above all) stay in the file, and 7-Zip lists them
// under "[DELETED]". Every image is exported in order, with the file's own compression, into a new
// file that replaces the old one once it is complete. Not for ESD / split / bootable WIMs (those
// are returned untouched).
[[nodiscard]] Result<void> optimizeWim(const std::filesystem::path& wim, const TaskContext& task);

// Removes edition `index` from a writable WIM (not an ESD, not a file inside an ISO).
[[nodiscard]] Result<void> deleteImage(const std::filesystem::path& wim, int index);

} // namespace wl::core
