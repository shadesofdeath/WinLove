#include "core/image/icons/IconSource.h"

#include "base/File.h"
#include "base/Log.h"
#include "core/image/ImageFiles.h"
#include "core/system/Picture.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <format>
#include <functional>
#include <iterator>

namespace wl::core {

namespace {

int depthOf(const IconImage& image) {
    return image.png ? 32 : std::min<int>(image.bitCount, 32);
}

int extentOf(const IconImage& image) {
    return std::max<int>(image.width, image.height);
}

bool hasSize(const std::vector<IconImage>& images, int size) {
    return std::ranges::any_of(images, [&](const IconImage& i) { return i.width == size && i.height == size; });
}

// Of the deepest images (a 32-bit one over the 8-bit one of the same icon), the smallest at
// least `size` — shrinking keeps the detail — else the largest.
const IconImage* bestSource(const std::vector<IconImage>& images, int size) {
    int deepest = 0;
    for (const auto& i : images) {
        deepest = std::max(deepest, depthOf(i));
    }
    const IconImage* best = nullptr;
    for (const auto& i : images) {
        if (depthOf(i) != deepest) {
            continue;
        }
        if (!best) {
            best = &i;
            continue;
        }
        const int e = extentOf(i);
        const int b = extentOf(*best);
        const bool fits = e >= size;
        const bool bestFits = b >= size;
        if (fits != bestFits ? fits : (fits ? e < b : e > b)) {
            best = &i;
        }
    }
    return best;
}

// size × size pixels stored the way Windows' own icons are: 256 px and up as PNG, smaller as a
// 32-bit bitmap.
Result<IconImage> windowsIconImage(const std::string& bgra, int size) {
    if (size < 256) {
        return dibIconImage(bgra, size);
    }
    auto png = encodePngBgra(bgra, size);
    if (!png) {
        return std::unexpected(png.error());
    }
    IconImage img;
    img.data = std::move(*png);
    if (!describeIconImage(img)) {
        return fail(ErrorCode::Unknown, L"the PNG made for the icon does not read back", L"");
    }
    return img;
}

} // namespace

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
        auto img = windowsIconImage(*pixels, size);
        if (!img) {
            return std::unexpected(Error{img.error().code, img.error().message, file.wstring(), 0});
        }
        images.push_back(std::move(*img));
    }
    return images;
}

Result<std::vector<IconImage>> completeIconSizes(std::vector<IconImage> images, std::span<const int> sizes) {
    if (images.empty()) {
        return fail(ErrorCode::InvalidArgument, L"an icon needs at least one image", L"");
    }
    std::vector<IconImage> made;
    for (const int size : sizes) {
        if (size <= 0 || size > 1024) {
            return fail(ErrorCode::InvalidArgument, L"bad icon size", std::to_wstring(size));
        }
        if (hasSize(images, size) || hasSize(made, size)) {
            continue;
        }
        // One image as a .ico: WIC's icon decoder reads every kind (PNG, 32-bit, palette + mask).
        const IconImage* from = bestSource(images, size);
        auto pixels = pictureBytesBgraSquare(makeIco(std::vector<IconImage>{*from}), size);
        if (!pixels) {
            return std::unexpected(pixels.error());
        }
        auto img = windowsIconImage(*pixels, size);
        if (!img) {
            return std::unexpected(img.error());
        }
        made.push_back(std::move(*img));
    }
    if (made.empty()) {
        return images;
    }
    std::ranges::move(made, std::back_inserter(images));
    std::ranges::stable_sort(images, std::greater<>{}, extentOf);
    return images;
}

std::vector<int> iconSizesOf(const std::vector<IconImage>& images) {
    std::vector<int> sizes;
    for (const auto& i : images) {
        if (i.width > 0 && std::ranges::find(sizes, static_cast<int>(i.width)) == sizes.end()) {
            sizes.push_back(i.width);
        }
    }
    std::ranges::sort(sizes, std::greater<>{});
    return sizes;
}

Result<int> iconSourceLargest(const std::filesystem::path& file) {
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
        int largest = 0;
        for (const auto& i : *images) {
            largest = std::max(largest, extentOf(i));
        }
        return largest;
    }
    auto size = pictureSize(file);
    if (!size) {
        return std::unexpected(size.error());
    }
    return std::max(size->width, size->height);
}

Result<void> copyIconIntoImage(const std::filesystem::path& mountDir, std::wstring_view relative,
                               const std::filesystem::path& source) {
    auto images = loadIconSource(source);
    if (!images) {
        return std::unexpected(images.error());
    }
    const std::size_t given = images->size();
    auto full = completeIconSizes(std::move(*images), kIconSizes);
    if (!full) {
        return std::unexpected(full.error());
    }
    if (auto written = writeImageCopy(mountDir, relative, makeIco(*full)); !written) {
        return written;
    }
    log::info("icons", std::format(L"{} -> {}: {} image(s), {} made for the missing sizes", source.wstring(), relative, full->size(),
                                   full->size() - given));
    return {};
}

} // namespace wl::core
