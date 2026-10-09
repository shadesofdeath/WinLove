#pragma once
// SHA-256 of files (CNG) and checking one against a published value (D-058).
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace wl::core {

// SHA-256 of a file as lowercase hex, with progress; cancellable.
[[nodiscard]] Result<std::wstring> sha256File(const std::filesystem::path& file, const TaskContext& task);
// SHA-1 the same way: what older UUP sets publish (D-093).
[[nodiscard]] Result<std::wstring> sha1File(const std::filesystem::path& file, const TaskContext& task);

// What a pasted hash says, lower-case hex like sha256File: hex digits only (spaces, dashes, "SHA256:" and case ignored); empty
// when it is not 64 hex digits.
[[nodiscard]] std::wstring normalizeSha256(std::wstring_view text);

} // namespace wl::core
