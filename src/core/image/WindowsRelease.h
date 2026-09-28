#pragma once
// Marketing names for Windows builds: 26200 → "11 25H2", 19045 → "10 22H2".
#include <string>

namespace wl::core {

// "11 25H2", "10 22H2"; unknown builds fall back to "11 build 27000" / "10 build 18363".
[[nodiscard]] std::wstring releaseLabel(int build);
// "11 25H2 · 26200.8037" (+ " · ARM64" for non-x64) — the recent-list / breadcrumb form.
[[nodiscard]] std::wstring releaseSummary(int build, int spBuild, const wchar_t* architecture);

} // namespace wl::core
