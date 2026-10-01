#pragma once
// Pictures for the image (wallpaper, lock screen, account picture, OEM logo) through WIC: any
// format Windows can decode in, JPEG / PNG / BMP out, scaled to cover a size (centre crop).
#include "base/Result.h"

#include <filesystem>
#include <string>

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

} // namespace wl::core
