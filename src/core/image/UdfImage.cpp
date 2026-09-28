#include "core/image/UdfImage.h"

#include "base/Log.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <format>
#include <fstream>

namespace wl::core {

namespace {

constexpr std::uint32_t kSector = 2048;
constexpr std::uint32_t kAnchorSector = 256;

// Descriptor tag identifiers (ECMA-167 3/7.2.1, 4/7.2.1).
constexpr std::uint16_t kTagAnchor = 2;
constexpr std::uint16_t kTagPartition = 5;
constexpr std::uint16_t kTagLogicalVolume = 6;
constexpr std::uint16_t kTagTerminating = 8;
constexpr std::uint16_t kTagFileSet = 256;
constexpr std::uint16_t kTagFileIdentifier = 257;
constexpr std::uint16_t kTagAllocationExtent = 258;
constexpr std::uint16_t kTagFileEntry = 261;
constexpr std::uint16_t kTagExtendedFileEntry = 266;

template <class T>
T le(const std::byte* p) {
    T value{};
    std::memcpy(&value, p, sizeof(T));
    return value; // x64 is little-endian, like UDF
}

using Bytes = std::vector<std::byte>;

Result<Bytes> readBytes(const ByteSource& source, std::uint64_t offset, std::size_t length) {
    Bytes data(length);
    if (auto r = source.read(offset, data); !r) {
        return std::unexpected(r.error());
    }
    return data;
}

// Checks tag id + checksum (sum of tag bytes 0..15 except byte 4).
bool validTag(const std::byte* p, std::uint16_t expected) {
    if (le<std::uint16_t>(p) != expected) {
        return false;
    }
    std::uint8_t sum = 0;
    for (int i = 0; i < 16; ++i) {
        if (i != 4) {
            sum = static_cast<std::uint8_t>(sum + static_cast<std::uint8_t>(p[i]));
        }
    }
    return sum == static_cast<std::uint8_t>(p[4]);
}

// OSTA CS0 "compressed unicode": first byte 8 (Latin-1) or 16 (UTF-16BE).
std::wstring decodeDString(const std::byte* p, std::size_t length) {
    if (length == 0) {
        return {};
    }
    const auto id = static_cast<std::uint8_t>(p[0]);
    std::wstring text;
    if (id == 8) {
        for (std::size_t i = 1; i < length; ++i) {
            text.push_back(static_cast<wchar_t>(static_cast<std::uint8_t>(p[i])));
        }
    } else if (id == 16) {
        for (std::size_t i = 1; i + 1 < length; i += 2) {
            text.push_back(static_cast<wchar_t>((static_cast<std::uint8_t>(p[i]) << 8) | static_cast<std::uint8_t>(p[i + 1])));
        }
    }
    return text;
}

bool equalsNoCase(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}

std::vector<std::wstring> splitPath(std::wstring_view path) {
    std::vector<std::wstring> parts;
    std::wstring current;
    for (const wchar_t c : path) {
        if (c == L'/' || c == L'\\') {
            if (!current.empty()) {
                parts.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(std::move(current));
    }
    return parts;
}

// A file inside the ISO exposed as a ByteSource (reads map onto the file's extents).
class UdfFileSource final : public ByteSource {
public:
    UdfFileSource(std::shared_ptr<const ByteSource> iso, UdfImage::Node node)
        : m_iso(std::move(iso)), m_node(std::move(node)) {}

    std::uint64_t size() const override { return m_node.size; }

    Result<void> read(std::uint64_t offset, std::span<std::byte> out) const override {
        if (offset + out.size() > m_node.size) {
            return fail(ErrorCode::IoError, L"read past end of file", m_node.name);
        }
        if (!m_node.embedded.empty()) {
            std::memcpy(out.data(), m_node.embedded.data() + offset, out.size());
            return {};
        }
        std::uint64_t fileOffset = 0;
        std::size_t done = 0;
        for (const auto& extent : m_node.extents) {
            if (done == out.size()) {
                break;
            }
            const std::uint64_t extentEnd = fileOffset + extent.length;
            const std::uint64_t want = offset + done;
            if (want < extentEnd) {
                const std::uint64_t inExtent = want - fileOffset;
                const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(extent.length - inExtent, out.size() - done));
                if (auto r = m_iso->read(extent.offset + inExtent, out.subspan(done, chunk)); !r) {
                    return r;
                }
                done += chunk;
            }
            fileOffset = extentEnd;
        }
        if (done != out.size()) {
            return fail(ErrorCode::ParseError, L"file extents shorter than file size", m_node.name);
        }
        return {};
    }

private:
    std::shared_ptr<const ByteSource> m_iso;
    UdfImage::Node m_node;
};

} // namespace

Result<UdfImage> UdfImage::open(const std::filesystem::path& isoPath) {
    auto file = DiskFile::open(isoPath);
    if (!file) {
        return std::unexpected(file.error());
    }
    return open(std::shared_ptr<const ByteSource>(std::move(*file)));
}

Result<UdfImage> UdfImage::open(std::shared_ptr<const ByteSource> iso) {
    UdfImage image;
    image.m_iso = std::move(iso);
    const ByteSource& src = *image.m_iso;
    auto parseError = [](const wchar_t* what) { return fail(ErrorCode::ParseError, what, L"UDF"); };

    auto anchor = readBytes(src, std::uint64_t{kAnchorSector} * kSector, kSector);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }
    if (!validTag(anchor->data(), kTagAnchor)) {
        return fail(ErrorCode::Unsupported, L"not a UDF image (no anchor at sector 256)", L"UDF");
    }
    const auto vdsLength = le<std::uint32_t>(anchor->data() + 16);
    const auto vdsSector = le<std::uint32_t>(anchor->data() + 20);

    bool havePartition = false;
    bool haveVolume = false;
    std::uint32_t fileSetBlock = 0;
    for (std::uint32_t i = 0; i < vdsLength / kSector; ++i) {
        auto d = readBytes(src, std::uint64_t{vdsSector + i} * kSector, kSector);
        if (!d) {
            return std::unexpected(d.error());
        }
        const std::byte* p = d->data();
        const auto tag = le<std::uint16_t>(p);
        if (tag == kTagTerminating) {
            break;
        }
        if (tag == kTagPartition && validTag(p, kTagPartition)) {
            // Type 1 map points at the one partition; its physical start is all we need.
            image.m_partitionStart = le<std::uint32_t>(p + 188);
            havePartition = true;
        } else if (tag == kTagLogicalVolume && validTag(p, kTagLogicalVolume)) {
            if (le<std::uint32_t>(p + 212) != kSector) {
                return fail(ErrorCode::Unsupported, L"logical block size is not 2048", L"UDF");
            }
            const auto maps = le<std::uint32_t>(p + 268);
            if (maps != 1 || static_cast<std::uint8_t>(p[440]) != 1) {
                return fail(ErrorCode::Unsupported, L"only a single Type 1 partition map is supported", L"UDF");
            }
            fileSetBlock = le<std::uint32_t>(p + 248 + 4); // long_ad(FSD).location.lbn
            image.m_label = decodeDString(p + 84, static_cast<std::size_t>(static_cast<std::uint8_t>(p[84 + 127])));
            haveVolume = true;
        }
    }
    if (!havePartition || !haveVolume) {
        return parseError(L"volume descriptor sequence incomplete");
    }

    auto fsd = readBytes(src, image.blockOffset(fileSetBlock), kSector);
    if (!fsd) {
        return std::unexpected(fsd.error());
    }
    if (!validTag(fsd->data(), kTagFileSet)) {
        return parseError(L"file set descriptor not found");
    }
    image.m_rootBlock = le<std::uint32_t>(fsd->data() + 400 + 4); // root directory ICB long_ad lbn
    log::debug("udf", std::format(L"UDF volume '{}', partition at sector {}", image.m_label, image.m_partitionStart));
    return image;
}

std::uint64_t UdfImage::blockOffset(std::uint32_t logicalBlock) const noexcept {
    return (std::uint64_t{m_partitionStart} + logicalBlock) * kSector;
}

Result<UdfImage::Node> UdfImage::readFileEntry(std::uint32_t logicalBlock, std::wstring name) const {
    auto entry = readBytes(*m_iso, blockOffset(logicalBlock), kSector);
    if (!entry) {
        return std::unexpected(entry.error());
    }
    const std::byte* p = entry->data();
    const auto tag = le<std::uint16_t>(p);
    const bool extended = tag == kTagExtendedFileEntry;
    if (!(tag == kTagFileEntry || extended) || !validTag(p, tag)) {
        return fail(ErrorCode::ParseError, L"bad file entry", name);
    }
    Node node;
    node.name = std::move(name);
    node.directory = static_cast<std::uint8_t>(p[27]) == 4; // ICB tag file type 4 = directory
    node.size = le<std::uint64_t>(p + 56);
    const auto flags = le<std::uint16_t>(p + 34);
    const std::uint32_t eaLength = le<std::uint32_t>(p + (extended ? 208 : 168));
    const std::uint32_t adLength = le<std::uint32_t>(p + (extended ? 212 : 172));
    const std::size_t adStart = (extended ? 216u : 176u) + eaLength;
    if (adStart + adLength > kSector) {
        return fail(ErrorCode::ParseError, L"allocation descriptors overflow the entry", node.name);
    }

    const auto adType = flags & 0x7;
    if (adType == 3) { // data embedded in the entry itself
        node.embedded.assign(p + adStart, p + adStart + adLength);
        node.size = std::min<std::uint64_t>(node.size, adLength);
        return node;
    }
    if (adType != 0 && adType != 1) {
        return fail(ErrorCode::Unsupported, L"extended allocation descriptors", node.name);
    }
    const std::size_t adSize = adType == 0 ? 8 : 16;

    // Walk short_ad/long_ad lists; extent type 3 continues in another block (large files).
    Bytes block = std::move(*entry);
    std::size_t pos = adStart;
    std::size_t end = adStart + adLength;
    while (pos + adSize <= end) {
        const std::byte* ad = block.data() + pos;
        const auto raw = le<std::uint32_t>(ad);
        const std::uint32_t length = raw & 0x3FFFFFFFu;
        const std::uint32_t type = raw >> 30;
        const std::uint32_t location = le<std::uint32_t>(ad + 4);
        pos += adSize;
        if (length == 0) {
            break;
        }
        if (type == 3) {
            auto next = readBytes(*m_iso, blockOffset(location), kSector);
            if (!next || !validTag(next->data(), kTagAllocationExtent)) {
                return fail(ErrorCode::ParseError, L"bad allocation extent", node.name);
            }
            block = std::move(*next);
            pos = 24;
            end = 24 + le<std::uint32_t>(block.data() + 20);
            continue;
        }
        if (type == 0) { // recorded and allocated (1/2 = not recorded: sparse, reads as zeros — not used on ISOs)
            node.extents.push_back({blockOffset(location), length});
        }
    }
    return node;
}

Result<std::vector<std::byte>> UdfImage::readAll(const Node& node) const {
    std::vector<std::byte> data(static_cast<std::size_t>(node.size));
    UdfFileSource source(m_iso, node);
    if (auto r = source.read(0, data); !r) {
        return std::unexpected(r.error());
    }
    return data;
}

Result<std::vector<UdfImage::Node>> UdfImage::readDirectory(const Node& directory) const {
    auto data = readAll(directory);
    if (!data) {
        return std::unexpected(data.error());
    }
    std::vector<Node> children;
    std::size_t pos = 0;
    while (pos + 38 <= data->size()) {
        const std::byte* p = data->data() + pos;
        if (le<std::uint16_t>(p) != kTagFileIdentifier) {
            break;
        }
        const auto characteristics = static_cast<std::uint8_t>(p[18]);
        const auto nameLength = static_cast<std::uint8_t>(p[19]);
        const auto icbBlock = le<std::uint32_t>(p + 20 + 4);
        const auto implUseLength = le<std::uint16_t>(p + 36);
        const std::size_t total = (38u + implUseLength + nameLength + 3u) & ~std::size_t{3};
        const bool parent = (characteristics & 0x08) != 0;
        const bool deleted = (characteristics & 0x04) != 0;
        if (!parent && !deleted) {
            std::wstring name = decodeDString(p + 38 + implUseLength, nameLength);
            auto child = readFileEntry(icbBlock, std::move(name));
            if (!child) {
                return std::unexpected(child.error());
            }
            children.push_back(std::move(*child));
        }
        pos += total;
    }
    return children;
}

Result<std::vector<UdfImage::Node>> UdfImage::list(std::wstring_view directoryPath) const {
    auto directory = find(directoryPath);
    if (!directory) {
        return std::unexpected(directory.error());
    }
    if (!directory->directory) {
        return fail(ErrorCode::InvalidArgument, L"not a directory", std::wstring(directoryPath));
    }
    return readDirectory(*directory);
}

Result<UdfImage::Node> UdfImage::find(std::wstring_view path) const {
    auto current = readFileEntry(m_rootBlock, L"");
    if (!current) {
        return std::unexpected(current.error());
    }
    for (const auto& part : splitPath(path)) {
        if (!current->directory) {
            return fail(ErrorCode::NotFound, L"path goes through a file", std::wstring(path));
        }
        auto children = readDirectory(*current);
        if (!children) {
            return std::unexpected(children.error());
        }
        const auto it = std::ranges::find_if(*children, [&](const Node& n) { return equalsNoCase(n.name, part); });
        if (it == children->end()) {
            return fail(ErrorCode::NotFound, L"not found in ISO", std::wstring(path));
        }
        current = std::move(*it);
    }
    return current;
}

std::shared_ptr<const ByteSource> UdfImage::openFile(Node file) const {
    return std::make_shared<UdfFileSource>(m_iso, std::move(file));
}

Result<void> UdfImage::extract(const Node& file, const std::filesystem::path& destination, const TaskContext& task) const {
    if (file.directory) {
        return fail(ErrorCode::InvalidArgument, L"cannot extract a directory as a file", file.name);
    }
    const std::filesystem::path partial = destination.wstring() + L".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(ErrorCode::IoError, L"cannot create destination", partial.wstring());
        }
        UdfFileSource source(m_iso, file);
        constexpr std::size_t kChunk = 8u << 20;
        std::vector<std::byte> buffer(kChunk);
        for (std::uint64_t offset = 0; offset < file.size;) {
            if (auto c = task.cancel.check(file.name); !c) {
                out.close();
                std::error_code ignored;
                std::filesystem::remove(partial, ignored);
                return c;
            }
            const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, file.size - offset));
            if (auto r = source.read(offset, std::span(buffer).first(chunk)); !r) {
                return r;
            }
            out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(chunk));
            if (!out) {
                return fail(ErrorCode::IoError, L"write failed (disk full?)", partial.wstring());
            }
            offset += chunk;
            task.report(static_cast<double>(offset) / static_cast<double>(file.size), file.name);
        }
    }
    // Only a complete file gets the final name.
    std::error_code ec;
    std::filesystem::rename(partial, destination, ec);
    if (ec) {
        return fail(ErrorCode::IoError, L"cannot rename extracted file", destination.wstring(), ec.value());
    }
    return {};
}

} // namespace wl::core
