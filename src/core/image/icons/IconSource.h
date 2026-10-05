#pragma once
// D-068: an icon picked on this PC — a .ico as it is, or any picture Windows can decode (PNG,
// JPEG, BMP, …) turned into the sizes Windows' own icons carry: 16, 20, 24, 32, 40, 48 and 64 px
// as 32-bit bitmaps and 256 px as PNG (what imageres.dll.mun does). Pictures are fitted into the
// square (aspect kept, transparent margins).
#include "base/Result.h"
#include "core/image/icons/IconGroups.h"

#include <filesystem>
#include <vector>

namespace wl::core {

inline constexpr int kIconSizes[] = {256, 64, 48, 40, 32, 24, 20, 16};

[[nodiscard]] Result<std::vector<IconImage>> loadIconSource(const std::filesystem::path& file);
// size × size straight-alpha BGRA pixels (top row first) → a 32-bit DIB icon image.
[[nodiscard]] IconImage dibIconImage(const std::string& bgra, int size);

} // namespace wl::core
