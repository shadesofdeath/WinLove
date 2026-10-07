#pragma once
// What a DISM / WIMGAPI error means and what fixes it (docs/ENGINE.md "Hata kataloğu").
// Codes and texts come from the message tables of wimgapi.dll (0xC142xxxx), DismCore.dll and
// WimProvider.dll (0xC151xxxx) — dump them with tools/dump_dism_messages.py — plus the Win32
// codes DISM passes through in the mount folder (access denied, sharing violation, ...).
#include <cstdint>
#include <string>

namespace wl::core {

enum class Remedy : std::uint8_t {
    None,
    CloseOpenFiles,     // something (Explorer, a console, an editor) has files open in the mount folder
    RepairFolder,       // mount folder left inconsistent: inspect + repairMount (MountHealth.h)
    Remount,            // DismRemountImage
    UnmountFirst,       // an image is already mounted there / from that WIM
    ConvertEsd,         // ESD/LZMS cannot be mounted: export to WIM first
    CheckPermissions,   // no rights on the WIM or not elevated
    DisableScanners,    // antivirus / indexer interfering with the mount folder
    UseLocalFixedDrive, // mount folder on a volume without reparse points, removable or a root
    WaitForOther,       // another DISM operation / process is using the image
    FreeDiskSpace,
    NotSupported,       // split WIM mount, read-only commit, ...
    WimLibrary,         // no wimgapi at hand reads the file: another tool's ESD, or a changed DISM (D-081)
};

struct ErrorInfo {
    std::int32_t hresult = 0;
    const wchar_t* label = L"";  // short English label for logs ("partial unmount")
    Remedy remedy = Remedy::None;
    bool known = false;
};

[[nodiscard]] ErrorInfo explainError(std::int32_t hresult) noexcept;
[[nodiscard]] const wchar_t* remedyName(Remedy remedy) noexcept;
// The system's own text for the code: wimgapi.dll / DismCore.dll / system message tables.
[[nodiscard]] std::wstring systemMessage(std::int32_t hresult);

inline constexpr std::int32_t kPartialUnmount = static_cast<std::int32_t>(0xC1420117);
inline constexpr std::int32_t kFileInUse = static_cast<std::int32_t>(0xC1420112);
inline constexpr std::int32_t kCannotCommit = static_cast<std::int32_t>(0xC142011D);
inline constexpr std::int32_t kNotMounted = static_cast<std::int32_t>(0xC142010B);
inline constexpr std::int32_t kNotMountDir = static_cast<std::int32_t>(0xC142011C);

} // namespace wl::core
