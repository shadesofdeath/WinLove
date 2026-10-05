#pragma once
// D-068: icons inside a PE resource tree. An icon the shell shows ("imageres.dll,-3") is an
// RT_GROUP_ICON entry: a directory (GRPICONDIR) of images, each an RT_ICON entry (a DIB or a PNG).
// A .ico file is the same directory with the images inline.
//
// Replacing a group writes the new images over the group's RT_ICON ids (more images get fresh ids
// after the highest one; ids only this group used are dropped), for every language the group has.
// Nothing else in the tree changes.
#include "base/Result.h"
#include "core/image/icons/PeResources.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

inline constexpr std::uint16_t kRtIcon = 3;
inline constexpr std::uint16_t kRtGroupIcon = 14;

struct IconImage {
    std::uint16_t width = 0;  // pixels (256 for the "0" of the directory)
    std::uint16_t height = 0;
    std::uint8_t colors = 0;  // palette entries (0 for 8 bpp and up)
    std::uint16_t planes = 1;
    std::uint16_t bitCount = 0;
    bool png = false;
    std::string data; // the RT_ICON payload: a DIB (BITMAPINFOHEADER + XOR + AND) or a PNG
};

struct IconGroupInfo {
    ResourceKey key;
    int index = 0; // position among the groups: what "file,index" (index ≥ 0) means to the shell
    std::vector<std::uint16_t> languages;
    std::vector<IconImage> images; // first language's, data included
};

// Every icon group of the tree, in directory order. Images whose RT_ICON is missing are skipped.
[[nodiscard]] std::vector<IconGroupInfo> listIconGroups(const ResourceTree& tree);

// A .ico file ↔ images. parseIco checks every entry's bounds and reads the real size / depth from
// the image data (PNG IHDR or BITMAPINFOHEADER), not from the directory.
[[nodiscard]] Result<std::vector<IconImage>> parseIco(std::string_view ico);
[[nodiscard]] std::string makeIco(const std::vector<IconImage>& images);

// The group (all its languages) now shows `images`. NotFound when the tree has no such group.
[[nodiscard]] Result<void> replaceIconGroup(ResourceTree& tree, const ResourceKey& group, const std::vector<IconImage>& images);

// Image data → its size / depth (PNG or DIB); false when it is neither.
[[nodiscard]] bool describeIconImage(IconImage& image);

} // namespace wl::core
