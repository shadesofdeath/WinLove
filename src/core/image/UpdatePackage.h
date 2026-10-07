#pragma once
// P08: what an update package is, from its Microsoft file name — the catalog naming is stable:
//   windows11.0-kb5043080-x64_<hash>.msu        servicing stack / cumulative
//   windows11.0-kb5044030-x64-ndp481_<hash>.msu .NET Framework
// Kind: Language for Microsoft-Windows-Client-Language-Pack / -LanguageFeatures- cabs,
// SSU when the name says "ssu" or the KB is a known SSU pattern, .NET for "ndp", LCU for
// other windows1x.0 cumulative packages, Other for anything else (.cab language packs, …).
// DISM itself decides applicability at apply time; this is for ordering and early warnings.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

// Language: a language pack or a language feature (LanguagePacks.h, D-053) — after the servicing
// stack, before the cumulative update (Microsoft's order: the LCU updates what the pack adds).
// SafeOs: the Safe OS dynamic update — for the image's WinRE, not the image (D-080). Its file name
// does not say so: only the update catalog does.
enum class UpdateKind : std::uint8_t { Ssu, Lcu, DotNet, Other, Language, SafeOs };

struct UpdateInfo {
    std::filesystem::path path;
    std::wstring kb;          // "KB5044284" or empty
    UpdateKind kind = UpdateKind::Other;
    int targetWindows = 0;    // 10 / 11 from "windows10.0-" / "windows11.0-", 0 = unknown
    std::wstring architecture; // "x64", "arm64", "x86" or empty
    std::uint64_t size = 0;
};

[[nodiscard]] UpdateInfo analyzeUpdate(const std::filesystem::path& file);
[[nodiscard]] const wchar_t* updateKindKey(UpdateKind kind) noexcept; // "ssu" "lcu" "dotnet" "other" "language" "safeos"
[[nodiscard]] UpdateKind updateKindFromKey(std::wstring_view key) noexcept;
[[nodiscard]] bool isUpdateFile(const std::filesystem::path& file);    // .msu / .cab
// *.msu / *.cab under `folder` (recursive), sorted by name.
[[nodiscard]] std::vector<std::filesystem::path> scanUpdates(const std::filesystem::path& folder);
// Windows generation of an image build: 11 from build 22000 on, otherwise 10.
[[nodiscard]] constexpr int windowsGeneration(int build) noexcept {
    return build >= 22000 ? 11 : 10;
}

} // namespace wl::core
