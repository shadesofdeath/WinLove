#pragma once
// P07 data: provisioned AppX packages of a mounted image with their on-disk size. Sizes come from
// <mount>\Program Files\WindowsApps\<Name>_* (the main package plus its resource/arch packages).
// That folder is locked down by ACL even for administrators, so it is walked with backup semantics
// (SeBackupPrivilege) — no ownership changes, read-only.
#include "core/image/SystemComponents.h"
#include "core/image/dism/Dism.h"

#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct AppxComponent {
    AppxEntry package;
    std::uint64_t size = 0; // 0 = unknown
    // D-082: the packages it cannot run without — <PackageDependency Name="…"> of its AppxManifest.xml
    // ("Microsoft.VCLibs.140.00", "Microsoft.UI.Xaml.2.8"), identity names, sorted, no repeats.
    std::vector<std::wstring> needs;
};

// The <PackageDependency Name="…"> of an AppxManifest.xml (any namespace prefix), in file order.
[[nodiscard]] std::vector<std::wstring> parseAppxDependencies(std::string_view manifestXml);

[[nodiscard]] Result<std::vector<AppxComponent>> readAppx(Dism& dism, const std::filesystem::path& mountDir,
                                                          const TaskContext& task);


// ---- removal without DISM ------------------------------------------------------------------
// DismRemoveProvisionedAppxPackage refuses a few apps outright (0x80073CFA within milliseconds:
// Microsoft.SecHealthUI and Microsoft.DesktopAppInstaller on 26100+, the latter on Windows 10
// 22H2 too). What DISM does for the apps it does remove was read off an image before and after a
// run of 45 removals (ENGINE.md, 2026-09-30) and is done here by hand, as a ComponentRecipe:
//   registry  …\Appx\AppxAllUserStore\Applications\<full name>   deleted
//             …\Appx\AppxAllUserStore\Staged\<family>            deleted
//             …\Appx\AppxAllUserStore\Deprovisioned\<family>     created (Windows does not bring it back)
//   files     Program Files\WindowsApps\<every package staged for the family>
//             ProgramData\Microsoft\Windows\ClipSVC\Install\Apps\<family, lower case>.xml  (its license)
// Frameworks the app depended on stay (DISM removes those nothing refers to any more; here they
// are left alone — a few megabytes against the risk of taking one another app needs).

// "Microsoft.SecHealthUI_1000.26100.8036.0_x64__8wekyb3d8bbwe" → "Microsoft.SecHealthUI_8wekyb3d8bbwe".
// Empty when the text is not a package full name (Name_Version_Arch_ResourceId_PublisherId, no
// path characters).
[[nodiscard]] std::wstring appxFamilyName(std::wstring_view packageFullName);
// The recipe for one app. `staged`: the package full names under Staged\<family> (the app's bundle,
// its architecture and resource packages, an older version still on disk); names that are not
// package names are ignored. Empty title when `packageFullName` is not one.
[[nodiscard]] ComponentRecipe appxRemovalRecipe(std::wstring_view packageFullName, const std::vector<std::wstring>& staged);
// The names under Staged\<family> of a mounted image (loads its SOFTWARE hive; elevated process,
// no DISM session open on the image). A family that is not staged gives an empty list.
[[nodiscard]] Result<std::vector<std::wstring>> readStagedAppx(const std::filesystem::path& mountDir,
                                                               std::wstring_view family);
// Reads what is staged and runs the recipe. The session is suspended and reopened around the hive
// work, as for any system component.
[[nodiscard]] Result<void> removeAppxNative(DismSession& session, std::wstring_view packageFullName,
                                            const TaskContext& task);
// DISM's "removal failed" for an app it will not let go: the cue to do it natively.
inline constexpr std::int32_t kAppxRemovalRefused = static_cast<std::int32_t>(0x80073CFA);

} // namespace wl::core
