#pragma once
// Checking a file against a published SHA-256 (D-058). The hash itself: sha256File (IsoBuilder.h).
#include <string>
#include <string_view>

namespace wl::core {

// What a pasted hash says, lower-case hex like sha256File: hex digits only (spaces, dashes, "SHA256:" and case ignored); empty
// when it is not 64 hex digits.
[[nodiscard]] std::wstring normalizeSha256(std::wstring_view text);

} // namespace wl::core
