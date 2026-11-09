#pragma once
// D-114: this PC's power plans (the reference tool's Kayıt Defteri → "Ana bilgisayarı içe aktar: Güç planı"):
// listed through the power API (PowerEnumerate / PowerReadFriendlyName, names in this PC's
// language), one exported with Windows' own powercfg /export into a .pow file — which the
// Post-Setup PowerPlan step imports and makes active on the installed system.
#include "base/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct PowerPlan {
    std::wstring guid; // "381b4222-f694-41f0-9685-ff5bb260df2e"
    std::wstring name;
    bool active = false;
};

// Windows' own schemes every install has (powercfg /setactive takes these GUIDs).
inline constexpr const wchar_t* kPlanBalanced = L"381b4222-f694-41f0-9685-ff5bb260df2e";
inline constexpr const wchar_t* kPlanHighPerformance = L"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
inline constexpr const wchar_t* kPlanPowerSaver = L"a1841308-3541-4fab-bc81-f71556f20b4a";
inline constexpr const wchar_t* kPlanUltimate = L"e9a42b02-d5df-448d-aa00-03f14749eb61"; // a template: duplicated, then active

[[nodiscard]] std::vector<PowerPlan> hostPowerPlans();
[[nodiscard]] Result<void> exportHostPowerPlan(const std::wstring& guid, const std::filesystem::path& file);
// The first-boot command that makes one of Windows' schemes active ("Nihai Performans" is
// duplicated from its template first, under a fixed GUID of WinLove's).
[[nodiscard]] std::wstring powerPlanCommand(const std::wstring& schemeGuid);

} // namespace wl::core
