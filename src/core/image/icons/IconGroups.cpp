#include "core/image/icons/IconGroups.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace wl::core {

namespace {

constexpr std::size_t kDirHeader = 6;
constexpr std::size_t kGroupEntry = 14;
constexpr std::size_t kIcoEntry = 16;
constexpr std::size_t kMaxImages = 64;

template <class T>
bool readAt(std::string_view bytes, std::size_t offset, T& out) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return false;
    }
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return true;
}

template <class T>
void put(std::string& out, T value) {
    char raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    out.append(raw, sizeof(T));
}

std::uint32_t bigEndian(std::string_view bytes, std::size_t at) {
    const auto b = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + i])); };
    return (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | b(3);
}

struct GroupEntry {
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t colors = 0;
    std::uint16_t planes = 0;
    std::uint16_t bitCount = 0;
    std::uint32_t bytes = 0;
    std::uint16_t id = 0;
};

std::vector<GroupEntry> parseGroup(std::string_view data) {
    std::vector<GroupEntry> out;
    std::uint16_t type = 0;
    std::uint16_t count = 0;
    if (!readAt(data, 2, type) || !readAt(data, 4, count) || type != 1 || count > kMaxImages ||
        data.size() < kDirHeader + std::size_t{count} * kGroupEntry) {
        return out;
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t at = kDirHeader + std::size_t{i} * kGroupEntry;
        GroupEntry e;
        readAt(data, at, e.width);
        readAt(data, at + 1, e.height);
        readAt(data, at + 2, e.colors);
        readAt(data, at + 4, e.planes);
        readAt(data, at + 6, e.bitCount);
        readAt(data, at + 8, e.bytes);
        readAt(data, at + 12, e.id);
        out.push_back(e);
    }
    return out;
}

std::uint8_t dirSize(std::uint16_t pixels) {
    return pixels >= 256 ? 0 : static_cast<std::uint8_t>(pixels);
}

std::string makeGroup(const std::vector<IconImage>& images, const std::vector<std::uint16_t>& ids) {
    std::string out;
    put<std::uint16_t>(out, 0);
    put<std::uint16_t>(out, 1);
    put<std::uint16_t>(out, static_cast<std::uint16_t>(images.size()));
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto& img = images[i];
        put<std::uint8_t>(out, dirSize(img.width));
        put<std::uint8_t>(out, dirSize(img.height));
        put<std::uint8_t>(out, img.colors);
        put<std::uint8_t>(out, 0);
        put<std::uint16_t>(out, img.planes);
        put<std::uint16_t>(out, img.bitCount);
        put<std::uint32_t>(out, static_cast<std::uint32_t>(img.data.size()));
        put<std::uint16_t>(out, ids[i]);
    }
    return out;
}

const ResourceLanguage* iconData(const ResourceType* icons, std::uint16_t id, std::uint16_t language) {
    if (!icons) {
        return nullptr;
    }
    for (const auto& n : icons->names) {
        if (!n.key.named() && n.key.id == id && !n.languages.empty()) {
            for (const auto& l : n.languages) {
                if (l.language == language) {
                    return &l;
                }
            }
            return &n.languages.front();
        }
    }
    return nullptr;
}

// Every RT_ICON id the groups refer to (except `skip`), with how many group directories do.
std::map<std::uint16_t, int> iconReferences(const ResourceTree& tree, const ResourceKey* skip) {
    std::map<std::uint16_t, int> refs;
    if (const auto* groups = tree.type(kRtGroupIcon)) {
        for (const auto& g : groups->names) {
            if (skip && g.key == *skip) {
                continue;
            }
            for (const auto& l : g.languages) {
                for (const auto& e : parseGroup(l.data)) {
                    ++refs[e.id];
                }
            }
        }
    }
    return refs;
}

} // namespace

bool describeIconImage(IconImage& image) {
    const std::string_view d = image.data;
    static constexpr unsigned char kPng[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() >= 26 && std::memcmp(d.data(), kPng, 8) == 0 && d.substr(12, 4) == "IHDR") {
        const std::uint32_t w = bigEndian(d, 16);
        const std::uint32_t h = bigEndian(d, 20);
        if (w == 0 || h == 0 || w > 1024 || h > 1024) {
            return false;
        }
        image.png = true;
        image.width = static_cast<std::uint16_t>(w);
        image.height = static_cast<std::uint16_t>(h);
        image.colors = 0;
        image.planes = 1;
        const auto depth = static_cast<std::uint8_t>(d[24]);
        const auto colorType = static_cast<std::uint8_t>(d[25]);
        const int channels = colorType == 6 ? 4 : colorType == 2 ? 3 : colorType == 4 ? 2 : 1;
        image.bitCount = static_cast<std::uint16_t>(depth * channels);
        return true;
    }
    std::uint32_t headerSize = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint16_t planes = 0;
    std::uint16_t bitCount = 0;
    if (!readAt(d, 0, headerSize) || headerSize < 40 || headerSize > d.size() || !readAt(d, 4, width) ||
        !readAt(d, 8, height) || !readAt(d, 12, planes) || !readAt(d, 14, bitCount)) {
        return false;
    }
    if (width <= 0 || width > 1024 || height <= 0 || height > 2048 ||
        (bitCount != 1 && bitCount != 4 && bitCount != 8 && bitCount != 16 && bitCount != 24 && bitCount != 32)) {
        return false;
    }
    image.png = false;
    image.width = static_cast<std::uint16_t>(width);
    image.height = static_cast<std::uint16_t>(height / 2);
    image.planes = planes == 0 ? 1 : planes;
    image.bitCount = bitCount;
    image.colors = bitCount < 8 ? static_cast<std::uint8_t>(1u << bitCount) : 0;
    return true;
}

std::vector<IconGroupInfo> listIconGroups(const ResourceTree& tree) {
    std::vector<IconGroupInfo> out;
    const auto* groups = tree.type(kRtGroupIcon);
    if (!groups) {
        return out;
    }
    const auto* icons = tree.type(kRtIcon);
    // Directory order is named first, then ids — the order EnumResourceNames (and the shell's
    // positive index) uses.
    std::vector<const ResourceName*> ordered;
    for (const auto& g : groups->names) {
        if (g.key.named()) {
            ordered.push_back(&g);
        }
    }
    for (const auto& g : groups->names) {
        if (!g.key.named()) {
            ordered.push_back(&g);
        }
    }
    int index = 0;
    for (const auto* g : ordered) {
        IconGroupInfo info;
        info.key = g->key;
        info.index = index++;
        for (const auto& l : g->languages) {
            info.languages.push_back(l.language);
        }
        if (!g->languages.empty()) {
            const auto& first = g->languages.front();
            for (const auto& e : parseGroup(first.data)) {
                const auto* leaf = iconData(icons, e.id, first.language);
                if (!leaf) {
                    continue;
                }
                IconImage img;
                img.data = leaf->data;
                if (!describeIconImage(img)) {
                    img.width = e.width == 0 ? 256 : e.width;
                    img.height = e.height == 0 ? 256 : e.height;
                    img.bitCount = e.bitCount;
                    img.colors = e.colors;
                    img.planes = e.planes;
                }
                info.images.push_back(std::move(img));
            }
        }
        out.push_back(std::move(info));
    }
    return out;
}

Result<std::vector<IconImage>> parseIco(std::string_view ico) {
    std::uint16_t reserved = 1;
    std::uint16_t type = 0;
    std::uint16_t count = 0;
    if (!readAt(ico, 0, reserved) || !readAt(ico, 2, type) || !readAt(ico, 4, count) || reserved != 0 || type != 1) {
        return fail(ErrorCode::ParseError, L"not an .ico file", L"");
    }
    if (count == 0 || count > kMaxImages || ico.size() < kDirHeader + std::size_t{count} * kIcoEntry) {
        return fail(ErrorCode::ParseError, L"bad .ico directory", L"");
    }
    std::vector<IconImage> images;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t at = kDirHeader + std::size_t{i} * kIcoEntry;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
        readAt(ico, at + 8, size);
        readAt(ico, at + 12, offset);
        if (size == 0 || offset > ico.size() || ico.size() - offset < size) {
            return fail(ErrorCode::ParseError, L"an .ico image lies outside the file", L"");
        }
        IconImage img;
        img.data.assign(ico.data() + offset, size);
        if (!describeIconImage(img)) {
            return fail(ErrorCode::ParseError, L"an .ico image is neither a PNG nor a bitmap", L"");
        }
        std::uint8_t colors = 0;
        readAt(ico, at + 2, colors);
        if (!img.png && img.bitCount < 8 && colors != 0) {
            img.colors = colors;
        }
        images.push_back(std::move(img));
    }
    return images;
}

std::string makeIco(const std::vector<IconImage>& images) {
    std::string out;
    put<std::uint16_t>(out, 0);
    put<std::uint16_t>(out, 1);
    put<std::uint16_t>(out, static_cast<std::uint16_t>(images.size()));
    std::uint32_t offset = static_cast<std::uint32_t>(kDirHeader + images.size() * kIcoEntry);
    for (const auto& img : images) {
        put<std::uint8_t>(out, dirSize(img.width));
        put<std::uint8_t>(out, dirSize(img.height));
        put<std::uint8_t>(out, img.colors);
        put<std::uint8_t>(out, 0);
        put<std::uint16_t>(out, img.planes);
        put<std::uint16_t>(out, img.bitCount);
        put<std::uint32_t>(out, static_cast<std::uint32_t>(img.data.size()));
        put<std::uint32_t>(out, offset);
        offset += static_cast<std::uint32_t>(img.data.size());
    }
    for (const auto& img : images) {
        out += img.data;
    }
    return out;
}

Result<void> replaceIconGroup(ResourceTree& tree, const ResourceKey& group, const std::vector<IconImage>& images) {
    if (images.empty() || images.size() > kMaxImages) {
        return fail(ErrorCode::InvalidArgument, L"an icon needs 1 to 64 images", group.text());
    }
    const auto* existing = tree.type(kRtGroupIcon);
    if (!existing || std::ranges::none_of(existing->names, [&](const ResourceName& g) {
            return g.key == group && !g.languages.empty();
        })) {
        return fail(ErrorCode::NotFound, L"no such icon group", group.text());
    }
    // First: adding the RT_ICON type moves the types after it (the group pointer below would dangle).
    auto& icons = tree.ensureType(kRtIcon);
    auto* groups = tree.type(kRtGroupIcon);
    ResourceName* target = nullptr;
    if (groups) {
        for (auto& g : groups->names) {
            if (g.key == group) {
                target = &g;
            }
        }
    }
    if (!target || target->languages.empty()) {
        return fail(ErrorCode::NotFound, L"no such icon group", group.text());
    }
    const auto others = iconReferences(tree, &group);
    std::set<std::uint16_t> oldIds;
    for (const auto& l : target->languages) {
        for (const auto& e : parseGroup(l.data)) {
            oldIds.insert(e.id);
        }
    }
    std::uint32_t nextId = 0;
    for (const auto& n : icons.names) {
        if (!n.key.named()) {
            nextId = std::max<std::uint32_t>(nextId, n.key.id);
        }
    }
    std::set<std::uint16_t> taken; // ids this replacement already wrote
    for (auto& language : target->languages) {
        const auto old = parseGroup(language.data);
        std::vector<std::uint16_t> ids;
        for (std::size_t i = 0; i < images.size(); ++i) {
            std::uint16_t id = 0;
            if (i < old.size() && !others.contains(old[i].id) && !taken.contains(old[i].id) && old[i].id != 0) {
                id = old[i].id;
            } else {
                if (++nextId > 0xFFFF) {
                    return fail(ErrorCode::InvalidArgument, L"no free icon ids left", group.text());
                }
                id = static_cast<std::uint16_t>(nextId);
            }
            taken.insert(id);
            ids.push_back(id);
            auto& name = ensureName(icons, id);
            if (name.languages.empty()) {
                name.languages.push_back(ResourceLanguage{language.language, 0, images[i].data});
            } else {
                for (auto& leaf : name.languages) {
                    leaf.data = images[i].data;
                }
            }
        }
        language.data = makeGroup(images, ids);
    }
    // Icons only this group used, and no longer does, go.
    const auto now = iconReferences(tree, nullptr);
    std::erase_if(icons.names, [&](const ResourceName& n) {
        return !n.key.named() && oldIds.contains(n.key.id) && !now.contains(n.key.id);
    });
    return {};
}

} // namespace wl::core
