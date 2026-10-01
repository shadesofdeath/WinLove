#pragma once
// Whole-file reads and safe whole-file writes, used by every layer that keeps small files
// (settings, presets, answer files, recent list, catalogs, .reg / .inf / .xml inputs).
#include "base/Result.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace wl {

// The file's bytes; NotFound / IoError otherwise.
[[nodiscard]] Result<std::string> readFileBytes(const std::filesystem::path& file);
// Writes `bytes` to "<file>.tmp", then replaces `file` with it: a crash or a full disk never leaves
// a half-written file behind.
[[nodiscard]] Result<void> writeFileAtomic(const std::filesystem::path& file, std::string_view bytes);

} // namespace wl
