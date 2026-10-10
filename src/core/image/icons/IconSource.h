#pragma once
// D-068: an icon picked on this PC — a .ico as it is, or any picture Windows can decode (PNG,
// JPEG, BMP, …) turned into the sizes Windows' own icons carry: 16, 20, 24, 32, 40, 48 and 64 px
// as 32-bit bitmaps and 256 px as PNG (what imageres.dll.mun does). Pictures are fitted into the
// square (aspect kept, transparent margins).
//
// D-105: what reaches the image is completed to the sizes the shell asks for — a .ico with one
// size would otherwise be stretched by Windows for every other view (blurred or blocky).
#include "base/Result.h"
#include "core/image/icons/IconGroups.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace wl::core {

inline constexpr int kIconSizes[] = {256, 64, 48, 40, 32, 24, 20, 16};

[[nodiscard]] Result<std::vector<IconImage>> loadIconSource(const std::filesystem::path& file);
// size × size straight-alpha BGRA pixels (top row first) → a 32-bit DIB icon image.
[[nodiscard]] IconImage dibIconImage(const std::string& bgra, int size);

// D-105: every size of `sizes` that no image of `images` has, made from its best image (of its
// deepest images, the smallest at least as large, else the largest) the way Windows' own icons
// carry them: 32-bit bitmaps, 256 px and up as PNG. What `images` has stays byte for byte. With
// sizes added the result is ordered largest first; otherwise it is `images` as given.
[[nodiscard]] Result<std::vector<IconImage>> completeIconSizes(std::vector<IconImage> images, std::span<const int> sizes);
// A group's distinct sizes, largest first.
[[nodiscard]] std::vector<int> iconSizesOf(const std::vector<IconImage>& images);
// The largest size `file` gives (.ico: its largest image; a picture: its longer side). Below 256
// the large views are made by enlarging it.
[[nodiscard]] Result<int> iconSourceLargest(const std::filesystem::path& file);

// D-105: an icon of this PC (.ico or a picture) into the image as a .ico completed to
// kIconSizes — the redirect mode's ProgramData\WinLove\Icons\<slot>.ico. Path rules and limit of
// copyImageFile.
[[nodiscard]] Result<void> copyIconIntoImage(const std::filesystem::path& mountDir, std::wstring_view relative,
                                             const std::filesystem::path& source);

} // namespace wl::core
