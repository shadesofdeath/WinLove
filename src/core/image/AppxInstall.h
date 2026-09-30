#pragma once
// D-050: apps put into the image offline — .appx / .msix and their bundles — so every new user
// gets them without the Store or a network (DISM calls this "provisioning"):
//   dism.exe /Image:<mount> /Add-ProvisionedAppxPackage /PackagePath:<app>
//            [/DependencyPackagePath:<framework>]… (/LicensePath:<xml> | /SkipLicense) /Region:all
// The package's manifest is read with Windows' own packaging API (AppxPackaging.h, COM): name,
// publisher, version, architecture and the frameworks it needs. Those are looked for next to it
// (and in a "Dependencies" folder, as Microsoft's and winget's downloads lay them out) by
// identity name, the architectures of the image kept. A *License*.xml next to it is its Store
// licence; without one the app is provisioned with /SkipLicense (sideloaded apps need none).
// In the queue: one AddAppx operation — target = the package file, value = JSON (dependencies,
// licence).
#include "core/image/dism/Dism.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct AppxPackageInfo {
    std::filesystem::path path;
    std::wstring name;             // identity: "Microsoft.WindowsTerminal"
    std::wstring displayName;      // "Terminal" (literal, or the identity when it is a resource reference)
    std::wstring publisher;        // "CN=Microsoft Corporation, O=…"
    std::wstring publisherDisplay; // "Microsoft Corporation"
    std::wstring version;          // "1.21.2361.0"
    std::vector<std::wstring> architectures; // "x64", "arm64", "x86", "neutral" (a bundle: every one in it)
    bool bundle = false;
    bool framework = false;
    struct Dependency {
        std::wstring name;         // "Microsoft.VCLibs.140.00.UWPDesktop"
        std::wstring minVersion;
    };
    std::vector<Dependency> dependencies;
};

[[nodiscard]] bool isAppxFile(const std::filesystem::path& file); // .appx .msix .appxbundle .msixbundle
[[nodiscard]] Result<AppxPackageInfo> readAppxPackage(const std::filesystem::path& file);

struct AppxInstall {
    std::filesystem::path package;
    std::vector<std::filesystem::path> dependencies;
    std::filesystem::path license; // empty: /SkipLicense
    std::vector<std::wstring> missing; // dependency names nothing next to it provides (shown, not fatal)
    // What the queue shows without opening the package again.
    std::wstring name;        // identity
    std::wstring displayName;
    std::wstring publisher;   // display name when the manifest has one
    std::wstring version;
    std::vector<std::wstring> architectures;
    bool framework = false;

    [[nodiscard]] bool operator==(const AppxInstall&) const = default;
};

// Dependencies and licence found next to `package` for an image of `imageArchitecture`
// ("x64" / "arm64" / "x86"; its neutral and x86 frameworks count on x64, x64 ones on arm64).
[[nodiscard]] Result<AppxInstall> planAppxInstall(const std::filesystem::path& package, std::wstring_view imageArchitecture);

[[nodiscard]] std::string appxInstallToJson(const AppxInstall& install); // without `package` (the op target)
[[nodiscard]] Result<AppxInstall> appxInstallFromJson(const std::filesystem::path& package, std::string_view json);

// The dism.exe arguments (unit-tested).
[[nodiscard]] std::wstring appxArguments(const AppxInstall& install);
[[nodiscard]] Result<void> provisionAppx(DismSession& session, const AppxInstall& install, const TaskContext& task);

} // namespace wl::core
