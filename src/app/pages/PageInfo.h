#pragma once
// The page catalog: navigation order and groups, title/description strings, icon, roadmap step.
// Single source for the nav rail, the title-bar breadcrumb, Ctrl+1..9, the command palette and
// `--page=<key>` in render mode.
#include "app/generated/StringKeys.g.h"
#include "ui/generated/Icons.g.h"

#include <optional>
#include <span>
#include <string_view>

namespace wl::app {

enum class PageId : std::uint8_t {
    Source,
    Images,
    Components,
    Apps, // D-050 / D-054
    Programs, // D-078 winget programs at the first sign-in
    Features,
    Updates,
    Languages, // D-053
    Drivers,
    Registry,
    Services,
    Tasks, // D-048
    Tweaks,
    StartMenu, // D-069 Başlat menüsü
    Hosts, // D-049
    Branding, // D-056 Kişiselleştirme
    Icons, // D-065 Simgeler
    Unattended,
    PostSetup,
    Files, // D-051
    Apply,
    Iso,
    Presets,
    Logs,
    Settings,
    About,
    Gallery, // developer widget gallery (Faz 1.8); not in the nav
    Count
};

struct PageInfo {
    PageId id;
    std::string_view key;          // "source" — used by --page=
    Str navLabel;
    Str title;
    std::optional<Str> description;
    ui::icons::Icon icon;
    int navGroup;                  // -1: not shown in the nav rail
    std::string_view roadmapStep;  // "P01"
};

[[nodiscard]] std::span<const PageInfo> allPages() noexcept;
// The nav rail's group headers (navGroup 0…kNavGroups-1).
inline constexpr int kNavGroups = 6;
[[nodiscard]] Str navGroupLabel(int group) noexcept;
[[nodiscard]] const PageInfo& pageInfo(PageId id) noexcept;
[[nodiscard]] std::optional<PageId> pageFromKey(std::string_view key) noexcept;

} // namespace wl::app
