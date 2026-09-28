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

// Removes edition `index` from a writable WIM (not an ESD, not a file inside an ISO).
[[nodiscard]] Result<void> deleteImage(const std::filesystem::path& wim, int index);

} // namespace wl::core
