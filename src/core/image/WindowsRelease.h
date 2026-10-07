#pragma once
// Marketing names for Windows builds: 26200 → "11 25H2", 19045 → "10 22H2".
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace wl::core {

// "11 25H2", "10 22H2"; unknown builds fall back to "11 build 27000" / "10 build 18363".
[[nodiscard]] std::wstring releaseLabel(int build);
// Windows 10 since 20H2 is the 2004 build (19041) with an enablement package switching on the
// release, and Microsoft's media keep 19041 in the image XML. From the names in an edition's
// Windows\servicing\Packages: "Microsoft-Windows-22H2Enablement-Package~….mum" → 19045 (what
// winver and Windows Update say). 0 when no enablement package is there.
inline constexpr int kWindows10Base = 19041;
[[nodiscard]] int enablementBuild(std::span<const std::wstring> packageFiles);
// "11 25H2 · 26200.8037" (+ " · ARM64" for non-x64) — the recent-list / breadcrumb form.
[[nodiscard]] std::wstring releaseSummary(int build, int spBuild, const wchar_t* architecture);

// AIO (D-077): the editions from `firstNew` on that share their name with another edition get the
// release in brackets — "Windows 11 Pro (24H2)", or the full version when that is the same too —
// so Setup's list tells them apart. Returns {index, new name}; nothing for the names already unique.
struct ImageInfo;
[[nodiscard]] std::vector<std::pair<int, std::wstring>> distinctEditionNames(const std::vector<ImageInfo>& images,
                                                                          int firstNew);

} // namespace wl::core
