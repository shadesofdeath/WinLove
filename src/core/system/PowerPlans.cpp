#include "core/system/PowerPlans.h"

#include "core/system/Process.h"

#include <windows.h>

#include <combaseapi.h>
#include <powrprof.h>

#include <cwctype>
#include <format>

namespace wl::core {

namespace {

std::wstring guidText(const GUID& g) {
    wchar_t buffer[64] = {};
    StringFromGUID2(g, buffer, 64);
    std::wstring text(buffer);
    if (text.size() > 2) {
        text = text.substr(1, text.size() - 2); // no braces: powercfg's form
    }
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

// WinLove's copy of "Ultimate Performance" (powercfg -duplicatescheme needs a new GUID).
constexpr const wchar_t* kUltimateCopy = L"e9a42b02-d5df-448d-aa00-03f14749eb62";

} // namespace

std::vector<PowerPlan> hostPowerPlans() {
    std::vector<PowerPlan> plans;
    GUID* activeScheme = nullptr;
    std::wstring active;
    if (PowerGetActiveScheme(nullptr, &activeScheme) == ERROR_SUCCESS && activeScheme) {
        active = guidText(*activeScheme);
        LocalFree(activeScheme);
    }
    for (ULONG i = 0;; ++i) {
        GUID scheme{};
        DWORD size = sizeof(scheme);
        if (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, i, reinterpret_cast<UCHAR*>(&scheme), &size) != ERROR_SUCCESS) {
            break;
        }
        DWORD bytes = 0;
        PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, nullptr, &bytes);
        std::wstring name(bytes / sizeof(wchar_t) + 1, L'\0');
        if (bytes > 0 &&
            PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, reinterpret_cast<UCHAR*>(name.data()), &bytes) == ERROR_SUCCESS) {
            name.resize(wcsnlen(name.c_str(), name.size()));
        } else {
            name.clear();
        }
        PowerPlan plan{guidText(scheme), name, false};
        plan.active = plan.guid == active;
        if (plan.name.empty()) {
            plan.name = plan.guid;
        }
        plans.push_back(std::move(plan));
    }
    return plans;
}

Result<void> exportHostPowerPlan(const std::wstring& guid, const std::filesystem::path& file) {
    if (guid.size() != 36 || guid.find_first_not_of(L"0123456789abcdefABCDEF-") != std::wstring::npos) {
        return fail(ErrorCode::InvalidArgument, L"not a power scheme GUID", guid);
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::remove(file, ec);
    auto powercfg = systemTool(L"powercfg.exe");
    if (!powercfg) {
        return std::unexpected(powercfg.error());
    }
    auto exit = runProcess(std::format(L"\"{}\" /export \"{}\" {}", *powercfg, file.wstring(), guid), [](std::string_view) {});
    if (!exit) {
        return std::unexpected(exit.error());
    }
    if (*exit != 0 || !std::filesystem::exists(file, ec)) {
        return fail(ErrorCode::IoError, L"powercfg could not export the power plan", guid, static_cast<std::int32_t>(*exit));
    }
    return {};
}

std::wstring powerPlanCommand(const std::wstring& schemeGuid) {
    if (schemeGuid == kPlanUltimate) {
        return std::format(L"cmd.exe /c powercfg -duplicatescheme {0} {1} & powercfg /setactive {1}", kPlanUltimate, kUltimateCopy);
    }
    return L"powercfg.exe /setactive " + schemeGuid;
}

} // namespace wl::core
