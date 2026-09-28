#include "core/image/WindowsRelease.h"

#include <array>
#include <format>
#include <string_view>

namespace wl::core {

namespace {

struct Release {
    int build;
    const wchar_t* name;
};

// Client releases; LTSC shares its build with the matching H2 release.
constexpr std::array<Release, 13> kReleases = {{
    {10240, L"1507"}, {14393, L"1607"}, {17763, L"1809"}, {19041, L"2004"}, {19042, L"20H2"},
    {19043, L"21H1"}, {19044, L"21H2"}, {19045, L"22H2"}, {22000, L"21H2"}, {22621, L"22H2"},
    {22631, L"23H2"}, {26100, L"24H2"}, {26200, L"25H2"},
}};

} // namespace

std::wstring releaseLabel(int build) {
    const int major = build >= 22000 ? 11 : 10;
    for (const auto& r : kReleases) {
        if (r.build == build) {
            return std::format(L"{} {}", major, r.name);
        }
    }
    return std::format(L"{} build {}", major, build);
}

std::wstring releaseSummary(int build, int spBuild, const wchar_t* architecture) {
    std::wstring text = std::format(L"{} · {}.{}", releaseLabel(build), build, spBuild);
    if (architecture && std::wstring_view(architecture) != L"x64") {
        text += L" · ";
        for (const wchar_t* c = architecture; *c; ++c) {
            text.push_back(static_cast<wchar_t>(towupper(*c)));
        }
    }
    return text;
}

} // namespace wl::core
