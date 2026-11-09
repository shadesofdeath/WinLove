#include "ui/render/IconTone.h"

namespace wl::ui {

IconTone iconTone(icons::Icon icon) noexcept {
    using I = icons::Icon;
    using C = tokens::Color;
    switch (icon) {
    // Files, folders, presets, logs: amber.
    case I::Folder:
    case I::OpenFolder:
    case I::File:
    case I::CabFile:
    case I::MsuFile:
    case I::InfFile:
    case I::RegFile:
    case I::ScriptFile:
    case I::XmlFile:
    case I::PresetBookmark:
    case I::LogTerminal:
    case I::Registry:
    case I::IconSet:
    case I::Personalize:
        return {C::IconAmber, true};
    case I::Pin:
    case I::Unpin:
    case I::QueueClock:
        return {C::IconAmber, false};
    // Warnings.
    case I::WarningTriangle:
    case I::RiskMedium:
    case I::ShieldWarning:
        return {C::IconAmber, true};
    // Go, done, protected: green.
    case I::ApplyPlay:
    case I::Play:
    case I::PostSetupRocket:
    case I::SuccessCircle:
    case I::ShieldCheck:
    case I::DefenderShield:
    case I::SecureBoot:
    case I::RiskLow:
        return {C::IconGreen, true};
    case I::Check:
    case I::Add:
    case I::Unlock:
    case I::SizeSaved:
    case I::Workflow:
        return {C::IconGreen, false};
    // Removal, stop, errors: red.
    case I::Delete:
    case I::ComponentsRemove:
    case I::ErrorOctagon:
    case I::RiskHigh:
    case I::Stop:
        return {C::IconRed, true};
    case I::Remove:
    case I::Unmount:
    case I::Power:
        return {C::IconRed, false};
    // Features, apps, automation, accounts: purple.
    case I::PuzzleFeatures:
    case I::AppxPackage:
    case I::Programs:
    case I::UnattendedRobot:
    case I::UserAccount:
    case I::DotnetGeneric:
        return {C::IconPurple, true};
    case I::TweaksSliders:
    case I::Keyboard:
    case I::DependencyLink:
        return {C::IconPurple, false};
    // Hardware and the system's machinery: teal.
    case I::DriverChip:
    case I::TpmChip:
    case I::DiskPartition:
    case I::Printer:
    case I::ServicesGear:
    case I::Settings:
        return {C::IconTeal, true};
    case I::TelemetrySignal:
    case I::Bluetooth:
        return {C::IconTeal, false};
    // Images, media, downloads, information: blue.
    case I::LayersEditions:
    case I::ImageWim:
    case I::DiscIso:
    case I::IsoBuild:
    case I::UsbDrive:
    case I::Source:
    case I::Mount:
    case I::Save:
    case I::AboutInfo:
    case I::InfoCircle:
    case I::StartMenu:
    case I::WindowsLogoGeneric:
    case I::EdgeBrowserGeneric:
    case I::WindowsDownload:
        return {C::IconBlue, true};
    case I::Download:
    case I::UpdateDownload:
    case I::Upload:
    case I::Import:
    case I::Export:
    case I::Link:
    case I::ExternalLink:
    case I::Network:
    case I::LanguageGlobe:
    case I::Refresh:
    case I::Search:
    case I::Filter:
    case I::Sort:
    case I::Copy:
    case I::Paste:
    case I::Duplicate:
    case I::Compare:
    case I::Edit:
    case I::Eye:
    case I::Undo:
    case I::Redo:
    case I::Expand:
    case I::Collapse:
    case I::CommandPalette:
        return {C::IconBlue, false};
    case I::QuestionCircle:
        return {C::IconSlate, true};
    default:
        return {C::IconSlate, false};
    }
}

} // namespace wl::ui
