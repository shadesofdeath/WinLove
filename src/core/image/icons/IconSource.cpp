#include "core/image/icons/IconSource.h"

#include "base/File.h"
#include "core/system/Picture.h"

#include <cstring>
#include <cwctype>

namespace wl::core {

IconImage dibIconImage(const std::string& bgra, int size) {
    const std::size_t row = static_cast<std::size_t>(size) * 4;
    const std::size_t maskRow = static_cast<std::size_t>((size + 31) / 32) * 4;
    IconImage img;
    std::string& d = img.data;
    d.assign(40 + row * size + maskRow * size, '\0');
    const std::uint32_t header = 40;
    const std::int32_t width = size;
    const std::int32_t height = size * 2; // XOR + AND
    const std::uint16_t planes = 1;
    const std::uint16_t bpp = 32;
    const std::uint32_t imageSize = static_cast<std::uint32_t>(row * size + maskRow * size);
    std::memcpy(d.data(), &header, 4);
    std::memcpy(d.data() + 4, &width, 4);
    std::memcpy(d.data() + 8, &height, 4);
    std::memcpy(d.data() + 12, &planes, 2);
    std::memcpy(d.data() + 14, &bpp, 2);
    std::memcpy(d.data() + 20, &imageSize, 4);
    // Bottom-up rows; the AND mask stays 0 (the alpha channel decides).
    for (int y = 0; y < size; ++y) {
        std::memcpy(d.data() + 40 + static_cast<std::size_t>(size - 1 - y) * row, bgra.data() + static_cast<std::size_t>(y) * row, row);
    }
    img.width = static_cast<std::uint16_t>(size);
    img.height = static_cast<std::uint16_t>(size);
    img.bitCount = 32;
    img.planes = 1;
    return img;
}

Result<std::vector<IconImage>> loadIconSource(const std::filesystem::path& file) {
    std::wstring ext = file.extension().wstring();
    for (auto& c : ext) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    if (ext == L".ico") {
        auto bytes = readFileBytes(file);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto images = parseIco(*bytes);
        if (!images) {
            return std::unexpected(Error{images.error().code, images.error().message, file.wstring(), 0});
        }
        return images;
    }
    std::vector<IconImage> images;
    for (const int size : kIconSizes) {
        auto pixels = pictureBgraSquare(file, size);
        if (!pixels) {
            return std::unexpected(pixels.error());
        }
        if (size >= 256) {
            auto png = encodePngBgra(*pixels, size);
            if (!png) {
                return std::unexpected(png.error());
            }
            IconImage img;
            img.data = std::move(*png);
            if (!describeIconImage(img)) {
                return fail(ErrorCode::Unknown, L"the PNG made from the picture does not read back", file.wstring());
            }
            images.push_back(std::move(img));
        } else {
            images.push_back(dibIconImage(*pixels, size));
        }
    }
    return images;
}

} // namespace wl::core
