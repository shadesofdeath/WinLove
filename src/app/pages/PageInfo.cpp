#include "app/pages/PageInfo.h"

#include <array>

namespace wl::app {

namespace {

using ui::icons::Icon;

// Order and groups follow the prototype nav (06_prototype NAV) and docs/ROADMAP.md step ids.
constexpr std::array<PageInfo, static_cast<std::size_t>(PageId::Count)> kPages = {{
    {PageId::Source, "source", Str::NavSource, Str::SourceTitle, Str::SourceDesc, Icon::Source, 0, "P01"},
    {PageId::Images, "images", Str::NavImages, Str::ImagesTitle, Str::ImagesDesc, Icon::LayersEditions, 0, "P02"},
    {PageId::Components, "components", Str::NavComponents, Str::ComponentsTitle, Str::ComponentsDesc, Icon::ComponentsRemove, 1, "P07"},
    {PageId::Apps, "apps", Str::NavApps, Str::AppsTitle, Str::AppsDesc, Icon::AppxPackage, 1, "D-050"},
    {PageId::Features, "features", Str::NavFeatures, Str::FeaturesTitle, Str::FeaturesDesc, Icon::PuzzleFeatures, 1, "P04"},
    {PageId::Updates, "updates", Str::NavUpdates, Str::UpdatesTitle, Str::UpdatesDesc, Icon::UpdateDownload, 1, "P08"},
    {PageId::Languages, "languages", Str::NavLanguages, Str::LanguagesTitle, Str::LanguagesDesc, Icon::LanguageGlobe, 1, "D-053"},
    {PageId::Drivers, "drivers", Str::NavDrivers, Str::DriversTitle, Str::DriversDesc, Icon::DriverChip, 1, "P09"},
    {PageId::Registry, "registry", Str::NavRegistry, Str::RegistryTitle, Str::RegistryDesc, Icon::Registry, 2, "P11"},
    {PageId::Services, "services", Str::NavServices, Str::ServicesTitle, Str::ServicesDesc, Icon::ServicesGear, 2, "P10"},
    {PageId::Tasks, "tasks", Str::NavTasks, Str::TasksTitle, Str::TasksDesc, Icon::QueueClock, 2, "D-048"},
    {PageId::Tweaks, "tweaks", Str::NavTweaks, Str::TweaksTitle, Str::TweaksDesc, Icon::TweaksSliders, 2, "P12"},
    {PageId::StartMenu, "startmenu", Str::NavStartmenu, Str::StartmenuTitle, Str::StartmenuDesc, Icon::Pin, 2, "D-069"},
    {PageId::Hosts, "hosts", Str::NavHosts, Str::HostsTitle, Str::HostsDesc, Icon::Network, 2, "D-049"},
    {PageId::Branding, "branding", Str::NavBranding, Str::BrandingTitle, Str::BrandingDesc, Icon::WindowsLogoGeneric, 2, "D-056"},
    {PageId::Icons, "icons", Str::NavIcons, Str::IconsTitle, Str::IconsDesc, Icon::DensityComfortable, 2, "D-065"},
    {PageId::Unattended, "unattended", Str::NavUnattended, Str::UnattendedTitle, Str::UnattendedDesc, Icon::UnattendedRobot, 3, "P13"},
    {PageId::PostSetup, "postsetup", Str::NavPostsetup, Str::PostsetupTitle, Str::PostsetupDesc, Icon::PostSetupRocket, 3, "P14"},
    {PageId::Files, "files", Str::NavFiles, Str::FilesTitle, Str::FilesDesc, Icon::Folder, 3, "D-051"},
    {PageId::Apply, "apply", Str::NavApply, Str::ApplyTitle, std::nullopt, Icon::ApplyPlay, 4, "P05"},
    {PageId::Iso, "iso", Str::NavIso, Str::IsoTitle, Str::IsoDesc, Icon::IsoBuild, 4, "P06"},
    {PageId::Presets, "presets", Str::NavPresets, Str::PresetsTitle, Str::PresetsDesc, Icon::PresetBookmark, 5, "P15"},
    {PageId::Logs, "logs", Str::NavLogs, Str::LogsTitle, Str::LogsDesc, Icon::LogTerminal, 5, "P03"},
    {PageId::Settings, "settings", Str::NavSettings, Str::SettingsTitle, Str::SettingsDesc, Icon::Settings, 5, "P16"},
    {PageId::About, "about", Str::NavAbout, Str::AboutTitle, std::nullopt, Icon::AboutInfo, 5, "P17"},
    {PageId::Gallery, "gallery", Str::AppName, Str::AppName, std::nullopt, Icon::DensityCompact, -1, "1.8"},
}};

} // namespace

std::span<const PageInfo> allPages() noexcept {
    return kPages;
}

const PageInfo& pageInfo(PageId id) noexcept {
    return kPages[static_cast<std::size_t>(id)];
}

std::optional<PageId> pageFromKey(std::string_view key) noexcept {
    for (const auto& page : kPages) {
        if (page.key == key) {
            return page.id;
        }
    }
    return std::nullopt;
}

} // namespace wl::app
