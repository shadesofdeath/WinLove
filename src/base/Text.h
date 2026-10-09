#pragma once
// Small text helpers every layer used to re-implement (lower-casing, case-insensitive compares,
// trimming). Case-insensitive here means Windows' ordinal rules (CompareStringOrdinal), the way
// file, registry and package names compare — not linguistic collation.
#include <string>
#include <string_view>

namespace wl::text {

// towlower on every character (ordinal, culture-independent): for identifiers — file, registry,
// package names. Only A–Z change; never use it to match what a person typed (fold below).
[[nodiscard]] std::wstring lower(std::wstring_view text);
// Search folding, one character per character (same length, so a match's position marks the
// original text): lower case for every script, Turkish I / İ / ı → "i", and ç ğ ö ş ü → c g o s u —
// "WINDOWS", "wındows", "İnternet" and "guncelleme" all find what they mean.
[[nodiscard]] std::wstring fold(std::wstring_view text);
// towupper on every character.
[[nodiscard]] std::wstring upper(std::wstring_view text);
[[nodiscard]] bool iequals(std::wstring_view a, std::wstring_view b) noexcept;
[[nodiscard]] bool istartsWith(std::wstring_view text, std::wstring_view prefix) noexcept;
[[nodiscard]] bool iendsWith(std::wstring_view text, std::wstring_view suffix) noexcept;
// Leading and trailing white space (spaces, tabs, CR, LF) removed.
[[nodiscard]] std::wstring_view trim(std::wstring_view text) noexcept;
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

} // namespace wl::text
