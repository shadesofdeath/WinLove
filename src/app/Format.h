#pragma once
// Locale-aware display formatting for the UI language (not the OS locale): WinLove in Turkish
// shows "6,72 GB" and "12 Eyl" even on an English Windows, and vice versa.
#include "app/Localization.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace wl::app {

// 1024-based, two decimals, locale decimal separator: "6,72 GB" (tr) / "6.72 GB" (en).
[[nodiscard]] std::wstring formatBytes(std::uint64_t bytes, Language language);

// Recent-list style: "bugün 14:02" / "today 14:02", "3 gün önce" / "3 days ago" (1–6 days),
// otherwise "12 Eyl" / "12 Sep" (+ year when not this year).
[[nodiscard]] std::wstring formatRecentTime(std::chrono::system_clock::time_point when, Language language,
                                            const Localization& strings,
                                            std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

[[nodiscard]] const wchar_t* localeName(Language language) noexcept; // "tr-TR" / "en-US"

// FILETIME (UTC) → local short date in the UI language: "07.03.2026" / "3/7/2026"; 0 → "—".
[[nodiscard]] std::wstring formatDate(std::uint64_t filetime, Language language);
// Thousands separators in the UI language: "145.176" (tr) / "145,176" (en).
[[nodiscard]] std::wstring formatCount(std::uint64_t value, Language language);

} // namespace wl::app
