#pragma once
// Marketing names for Windows builds: 26200 → "11 25H2", 19045 → "10 22H2".
#include <string>
#include <utility>
#include <vector>

namespace wl::core {

// "11 25H2", "10 22H2"; unknown builds fall back to "11 build 27000" / "10 build 18363".
[[nodiscard]] std::wstring releaseLabel(int build);
// "11 25H2 · 26200.8037" (+ " · ARM64" for non-x64) — the recent-list / breadcrumb form.
[[nodiscard]] std::wstring releaseSummary(int build, int spBuild, const wchar_t* architecture);

// AIO (D-077): the editions from `firstNew` on that share their name with another edition get the
// release in brackets — "Windows 11 Pro (24H2)", or the full version when that is the same too —
// so Setup's list tells them apart. Returns {index, new name}; nothing for the names already unique.
struct ImageInfo;
[[nodiscard]] std::vector<std::pair<int, std::wstring>> distinctEditionNames(const std::vector<ImageInfo>& images,
                                                                          int firstNew);

} // namespace wl::core
