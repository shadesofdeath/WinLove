#pragma once
// Kişiselleştirme: OEM information, the default wallpaper / lock screen / account picture, fonts.
//
// Pictures replace Windows' own defaults in the image, so every new user gets them and nothing
// depends on the answer file:
//   wallpaper   Windows\Web\Wallpaper\Windows\*.jpg, Windows\Web\4K\Wallpaper\Windows\*.jpg
//               (25H2: img0 = light theme, img19 = dark theme, + the 1920×1200 crops)
//   lockscreen  Windows\Web\Screen\img100.jpg (the default one) + PersonalizationCSP values
//   account     ProgramData\Microsoft\User Account Pictures\user*.png / user.bmp
//   oemlogo     Windows\System32\oemlogo.bmp (120×120, 24-bit) + OEMInformation\Logo
// Each file that is there is rewritten at the size and in the format it has (read from the image),
// as a NEW file: they are hard links into WinSxS (replaceImageFile).
#include "base/Result.h"
#include "core/image/RegistryEdit.h"
#include "core/tasks/Task.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

inline constexpr std::wstring_view kOemKey = L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OEMInformation";
inline constexpr std::wstring_view kOemLogoFile = L"Windows\\System32\\oemlogo.bmp";
inline constexpr std::wstring_view kPersonalizationCspKey =
    L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PersonalizationCSP";

enum class PictureSlot : std::uint8_t { Wallpaper, LockScreen, Account, OemLogo };
[[nodiscard]] std::wstring_view pictureSlotKey(PictureSlot slot) noexcept; // "wallpaper" …
[[nodiscard]] std::optional<PictureSlot> pictureSlotFromKey(std::wstring_view key) noexcept;

// The image files the slot rewrites (relative to the image root), those that are there.
// oemlogo: always kOemLogoFile (created when missing).
[[nodiscard]] std::vector<std::wstring> pictureTargets(const std::filesystem::path& mountDir, PictureSlot slot);

struct PictureResult {
    std::size_t files = 0;
    std::vector<RegistryWrite> registry; // for the caller's offline registry
};
[[nodiscard]] Result<PictureResult> applyPicture(const std::filesystem::path& mountDir, PictureSlot slot,
                                                 const std::filesystem::path& source, const TaskContext& task);

// Font: Windows\Fonts\<fontFileName(source)> + its Fonts value (the registry write is returned).
[[nodiscard]] Result<RegistryWrite> applyFont(const std::filesystem::path& mountDir, const std::filesystem::path& source);

// OEMInformation value names WinLove edits (besides Logo).
inline constexpr std::wstring_view kOemFields[] = {L"Manufacturer", L"Model", L"SupportPhone", L"SupportHours",
                                                   L"SupportURL"};

} // namespace wl::core
