#include "app/SystemInfo.h"

#include <windows.h>

#include <array>
#include <format>
#include <string_view>
#include <vector>

namespace wl::app {

std::wstring dismLibraryPath() {
    wchar_t system[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"dismapi.dll";
    }
    return std::wstring(system) + L"\\dismapi.dll";
}

std::wstring dismLibraryVersion() {
    const std::wstring path = dismLibraryPath();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return {};
    }
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || !info) {
        return {};
    }
    return std::format(L"{}.{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                       HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
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
