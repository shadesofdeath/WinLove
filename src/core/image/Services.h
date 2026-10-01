#pragma once
// P10: Win32 services of a mounted image, read from / written to its SYSTEM hive
// (ControlSet named by Select\Current, normally ControlSet001). Drivers (Type 1/2) are excluded.
// Start types: 0 boot, 1 system, 2 auto (+ DelayedAutostart=1 → delayed), 3 manual, 4 disabled.
#include "base/Result.h"
#include "core/image/RegistryEdit.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

enum class StartType : std::uint8_t { Boot, System, Auto, AutoDelayed, Manual, Disabled };

[[nodiscard]] const wchar_t* startTypeKey(StartType type) noexcept; // "auto", "autoDelayed", …
[[nodiscard]] std::optional<StartType> startTypeFromKey(std::wstring_view key) noexcept;

struct ServiceEntry {
    std::wstring name;        // key name (DiagTrack)
    std::wstring displayName; // resolved from the image's MUI files; key name when unresolvable
    std::wstring description;
    std::wstring imagePath;
    std::wstring account; // ObjectName (LocalSystem, NT AUTHORITY\LocalService…)
    std::uint32_t type = 0;
    StartType start = StartType::Manual;
    std::vector<std::wstring> dependsOn; // DependOnService
};

// Needs an elevated process (loads the hive). Sorted by display name.
[[nodiscard]] Result<std::vector<ServiceEntry>> readServices(const std::filesystem::path& mountDir);
// Sets Start (and DelayedAutostart) of one service in the image.
[[nodiscard]] Result<void> setServiceStart(const std::filesystem::path& mountDir, const std::wstring& name,
                                           StartType start);

// The registry writes behind a start type (HKLM\SYSTEM\CurrentControlSet\Services\<name>).
// A service key name: one path component (a preset must not reach "Svc\Parameters" or "..").
[[nodiscard]] bool validServiceName(std::wstring_view name) noexcept;
[[nodiscard]] std::vector<RegistryWrite> serviceStartWrites(const std::wstring& name, StartType start);

// Services whose DependOnService lists `name` (case-insensitive), directly or transitively.
[[nodiscard]] std::vector<std::wstring> dependentsOf(const std::vector<ServiceEntry>& services, std::wstring_view name);

// "@%SystemRoot%\system32\x.dll,-101" → "@<mount>\Windows\system32\x.dll,-101" (other text unchanged).
[[nodiscard]] std::wstring offlineResourcePath(std::wstring_view value, const std::filesystem::path& mountDir);

} // namespace wl::core
