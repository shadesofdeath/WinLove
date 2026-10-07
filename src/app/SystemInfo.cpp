#include "app/SystemInfo.h"

#include "core/image/dism/HostDism.h"
#include "core/system/Files.h"

#include <windows.h>

#include <array>
#include <format>
#include <string_view>
#include <vector>

namespace wl::app {

std::wstring dismLibraryPath() {
    // System32's, or the Windows ADK's when this PC's DISM is missing a part (D-081).
    return core::dismLocation().dismapi.wstring();
}

std::wstring dismLibraryVersion() {
    return core::fileVersion(core::dismLocation().dismapi);
}

bool dismFromAdk() {
    return core::dismLocation().adk;
}

std::wstring buildDate() {
    // __DATE__ is "Mmm dd yyyy" (day padded with a space).
    constexpr std::string_view date = __DATE__;
    constexpr std::array<std::string_view, 12> months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = 1;
    for (std::size_t i = 0; i < months.size(); ++i) {
        if (date.substr(0, 3) == months[i]) {
            month = static_cast<int>(i) + 1;
        }
    }
    const int day = (date[4] == ' ' ? 0 : date[4] - '0') * 10 + (date[5] - '0');
    const std::string_view year = date.substr(7, 4);
    return std::format(L"{}.{:02}.{:02}", std::wstring(year.begin(), year.end()), month, day);
}

std::wstring buildArchitecture() {
#if defined(_M_ARM64)
    return L"arm64";
#elif defined(_M_X64)
    return L"x64";
#else
    return L"x86";
#endif
}

} // namespace wl::app
