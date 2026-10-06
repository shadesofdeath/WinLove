#include "core/image/WindowsRelease.h"

#include "base/Text.h"
#include "core/image/ImageInfo.h"

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

std::vector<std::pair<int, std::wstring>> distinctEditionNames(const std::vector<ImageInfo>& images, int firstNew) {
    std::vector<std::pair<int, std::wstring>> renames;
    for (const auto& image : images) {
        if (image.index < firstNew) {
            continue;
        }
        bool clash = false;
        bool sameBuild = false;
        bool sameVersion = false;
        for (const auto& other : images) {
            if (other.index == image.index || !text::iequals(other.name, image.name)) {
                continue;
            }
            clash = true;
            sameBuild = sameBuild || other.build == image.build;
            sameVersion = sameVersion || other.versionString() == image.versionString();
        }
        if (!clash || sameVersion) {
            continue; // unique, or the very same Windows twice (a duplicate the user made)
        }
        // "11 24H2" → "24H2"; a build without a release name, or two of one release: the version.
        const std::wstring label = releaseLabel(image.build);
        const bool named = label.find(L"build") == std::wstring::npos;
        const std::wstring suffix = named && !sameBuild ? label.substr(label.find(L' ') + 1) : image.versionString();
        renames.emplace_back(image.index, std::format(L"{} ({})", image.name, suffix));
    }
    return renames;
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
