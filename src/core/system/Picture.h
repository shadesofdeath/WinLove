#pragma once
// Pictures for the image (wallpaper, lock screen, account picture, OEM logo) through WIC: any
// format Windows can decode in, JPEG / PNG / BMP out, scaled to cover a size (centre crop).
#include "base/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace wl::core {

enum class PictureFormat : std::uint8_t { Jpeg, Png, Bmp };

struct PictureSize {
    int width = 0;
    int height = 0;
};

[[nodiscard]] Result<PictureSize> pictureSize(const std::filesystem::path& file);

// The file's bytes in `format`. width / height 0: the picture's own size; otherwise scaled so it
// covers width × height and the overflow cropped evenly (the aspect is kept, nothing stretched).
// JPEG quality 0.95; BMP 24-bit (what OEMInformation\Logo reads); PNG 32-bit with alpha.
[[nodiscard]] Result<std::string> encodePicture(const std::filesystem::path& source, PictureFormat format, int width = 0,
                                                int height = 0);

// D-068: the picture fitted into a size × size square (aspect kept, transparent margins), as
// straight-alpha BGRA pixels, top row first (size * size * 4 bytes).
[[nodiscard]] Result<std::string> pictureBgraSquare(const std::filesystem::path& source, int size);
// D-105: the same for a picture held in memory (one image of an icon, wrapped as a .ico).
[[nodiscard]] Result<std::string> pictureBytesBgraSquare(std::string_view bytes, int size);
// size × size BGRA pixels (top row first) as a PNG file.
[[nodiscard]] Result<std::string> encodePngBgra(const std::string& pixels, int size);

} // namespace wl::core
