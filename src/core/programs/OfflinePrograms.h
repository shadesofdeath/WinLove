#pragma once
// D-104: install the Programs page's winget programs without internet. On the build machine
//   winget download --id <id> -e --download-directory <folder>\<id>
// fetches the installer and a manifest (InstallerType + the silent switch). The installers are
// embedded in the image (Windows\Setup\Scripts\WinLove\apps); at the first logon programs.ps1 runs
// each local installer (no winget, no network). winget download needs internet and winget at build.
#include "base/Result.h"
#include "core/tasks/Task.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct OfflineInstaller {
    std::wstring id;      // winget id, e.g. "7zip.7zip"
    std::wstring name;    // display name
    std::wstring file;    // the installer file name (sits in apps\<id>\ in the image)
    std::wstring command; // how to install it: a command line with a "{path}" placeholder for the file
    std::uint64_t bytes = 0;

    [[nodiscard]] bool operator==(const OfflineInstaller&) const = default;
};

// InstallerType + Silent switch out of a winget-download manifest (.yaml); both empty when absent.
struct ManifestInstall {
    std::wstring type;
    std::wstring silent;
};
[[nodiscard]] ManifestInstall parseWingetManifest(std::string_view yaml);

// The install command (with a "{path}" placeholder) for a downloaded installer of this type/silent
// switch. msi/wix -> msiexec; msix/appx -> Add-AppxPackage; everything else runs the file itself.
[[nodiscard]] std::wstring offlineInstallCommand(std::wstring_view type, std::wstring_view silent);

// winget download the program into <folder>\<id>\, read its manifest, and describe how to install it
// offline. An error when winget is missing, the download fails, or no installer came back.
[[nodiscard]] Result<OfflineInstaller> downloadProgramOffline(const std::wstring& id, const std::wstring& name,
                                                              const std::filesystem::path& folder,
                                                              const TaskContext& task);

// The offline installers as JSON (stored in programs.json so programs.ps1 can install them).
[[nodiscard]] std::string offlineInstallersToJson(const std::vector<OfflineInstaller>& installers);
[[nodiscard]] Result<std::vector<OfflineInstaller>> offlineInstallersFromJson(std::string_view json);

} // namespace wl::core
