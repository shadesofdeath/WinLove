#pragma once
// D-095: "Bu bilgisayardan al" — the programs installed on this PC that winget has, so a new
// Windows gets them at the first sign-in (Programlar, D-078). What `winget export` does, without
// winget: the programs Windows lists for removal (HKLM / HKCU Uninstall, both registry views) are
// matched against the signed index the Programs page keeps, the way winget correlates them:
//   1. the Uninstall key's name — an MSI product code or a plain name — against productcodes2;
//   2. otherwise the display name and publisher, normalized (lower case, letters and digits, no
//      version, architecture or legal suffix) against norm_names2 / norm_publishers2.
// Windows' own entries (SystemComponent, updates, entries with a parent) are left out.
#include "core/programs/Winget.h"

#include <optional>
#include <string>
#include <vector>

namespace wl::core {

struct InstalledProgram {
    std::wstring key;       // the Uninstall subkey: "{23170F69-…}", "7-Zip"
    std::wstring name;      // DisplayName
    std::wstring publisher;
    std::wstring version;   // DisplayVersion
    bool perUser = false;   // HKCU
};

struct InstalledMatch {
    InstalledProgram installed;
    WingetPackage package;
    bool byProductCode = false; // the strong match; otherwise by name
};

// Programs Windows lists as removable, Windows' own left out; each name once.
[[nodiscard]] std::vector<InstalledProgram> installedPrograms();

// ---- pure (unit-tested) ----
// winget-like normalization: "7-Zip 24.08 (x64)" → "7zip"; "Microsoft Corporation" → "microsoft".
[[nodiscard]] std::wstring normalizeProgramName(std::wstring_view name);
[[nodiscard]] std::wstring normalizePublisher(std::wstring_view publisher);
// The architecture a display name says ("(x64)", "64-bit"): "X64", "X86" or empty.
[[nodiscard]] std::wstring nameArchitecture(std::wstring_view name);
// Should an Uninstall entry with these values be offered at all?
[[nodiscard]] bool listedProgram(std::wstring_view displayName, std::uint32_t systemComponent,
                                 std::wstring_view parentKey, std::wstring_view releaseType);

// Every installed program winget has, each package once, by name.
[[nodiscard]] std::vector<InstalledMatch> matchInstalled(const WingetIndex& index,
                                                         const std::vector<InstalledProgram>& installed);

} // namespace wl::core
