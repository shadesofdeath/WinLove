#pragma once
// Byte / text encodings several engine parts need: Base64 (catalog hashes, answer-file
// passwords), lower-case hex (SHA-256), XML text escaping (answer files, Wi-Fi profiles).
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wl {

// Standard alphabet; stops at '='. Any other character outside the alphabet → empty result.
[[nodiscard]] std::vector<std::uint8_t> base64Decode(std::string_view text);
// Lower-case, two digits per byte.
[[nodiscard]] std::wstring hexLower(std::span<const std::uint8_t> bytes);
// & < > " (and ' when `apostrophe`) as entities, for XML element text and attribute values.
[[nodiscard]] std::wstring xmlEscape(std::wstring_view text, bool apostrophe = true);

} // namespace wl
