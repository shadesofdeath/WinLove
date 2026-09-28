#include "app/Format.h"

#include <windows.h>

#include <array>
#include <format>

namespace wl::app {

namespace {

SYSTEMTIME toLocalSystemTime(std::chrono::system_clock::time_point t) {
    const auto unix = std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
    // FILETIME: 100 ns ticks since 1601-01-01.
    const ULONGLONG ticks = (static_cast<ULONGLONG>(unix) + 11644473600ULL) * 10000000ULL;
    FILETIME utc{static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(ticks >> 32)};
    FILETIME local{};
    FileTimeToLocalFileTime(&utc, &local);
    SYSTEMTIME st{};
    FileTimeToSystemTime(&local, &st);
    return st;
}

std::wstring formatDate(const SYSTEMTIME& st, Language language, const wchar_t* pattern) {
    wchar_t buffer[64]{};
    GetDateFormatEx(localeName(language), 0, &st, pattern, buffer, 64, nullptr);
    return buffer;
}

// Whole local days between two local dates (ignores time of day).
int dayIndex(const SYSTEMTIME& st) {
    FILETIME ft{};
    SYSTEMTIME midnight = st;
    midnight.wHour = midnight.wMinute = midnight.wSecond = midnight.wMilliseconds = 0;
    SystemTimeToFileTime(&midnight, &ft);
    const ULONGLONG ticks = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<int>(ticks / (10000000ULL * 86400ULL));
}

} // namespace

const wchar_t* localeName(Language language) noexcept {
    return language == Language::Turkish ? L"tr-TR" : L"en-US";
}

std::wstring formatBytes(std::uint64_t bytes, Language language) {
    constexpr std::array<const wchar_t*, 5> kUnits = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < kUnits.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::wstring number = unit == 0 ? std::format(L"{}", bytes) : std::format(L"{:.2f}", value);
    if (language == Language::Turkish) {
        for (auto& c : number) {
            if (c == L'.') {
                c = L',';
            }
        }
    }
    return number + L" " + kUnits[unit];
}

std::wstring formatRecentTime(std::chrono::system_clock::time_point when, Language language,
                              const Localization& strings, std::chrono::system_clock::time_point now) {
    const SYSTEMTIME at = toLocalSystemTime(when);
    const SYSTEMTIME today = toLocalSystemTime(now);
    const int days = dayIndex(today) - dayIndex(at);
    if (days <= 0) {
        return std::format(L"{} {:02}:{:02}", strings.get(Str::SourceToday), at.wHour, at.wMinute);
    }
    if (days < 7) {
        return strings.format(Str::SourceDaysAgo, {{L"n", std::to_wstring(days)}});
    }
    return formatDate(at, language, at.wYear == today.wYear ? L"d MMM" : L"d MMM yyyy");
}

} // namespace wl::app
