#pragma once
// P09: what a driver package is, read from its .inf (no SetupAPI, no admin): [Version] Class /
// ClassGuid / Provider / DriverVer / CatalogFile (with %strings% resolved from [Strings]) and the
// architectures from the [Manufacturer] decorations (NTamd64, NTarm64, NTx86). INF files are
// UTF-16 LE (with BOM) or ANSI/UTF-8.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wl::core {

struct DriverInf {
    std::filesystem::path path;
    std::wstring className;   // "Net", "SCSIAdapter", "Display"… (empty if missing)
    std::wstring classGuid;
    std::wstring provider;    // "Intel"
    std::wstring version;     // "23.60.1.2"
    std::wstring date;        // "07/18/2024"
    std::wstring catalog;     // "netwtw10.cat"
    std::vector<std::wstring> architectures; // "amd64", "arm64", "x86" (empty: not decorated = any)
    std::uint64_t size = 0;   // share of its folder (folder size / INFs in it)
    [[nodiscard]] bool supports(std::wstring_view arch) const; // "x64" / "arm64" / "x86"
};

// Parses INF text (already decoded).
[[nodiscard]] DriverInf parseInfText(const std::wstring& text, const std::filesystem::path& path = {});
[[nodiscard]] DriverInf parseInf(const std::filesystem::path& path);
// Every .inf under `folder` (recursive), with sizes; sorted by class, then name.
[[nodiscard]] std::vector<DriverInf> scanDrivers(const std::filesystem::path& folder);

} // namespace wl::core
