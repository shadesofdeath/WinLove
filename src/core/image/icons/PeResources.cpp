#include "core/image/icons/PeResources.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <set>

namespace wl::core {

namespace {

constexpr std::uint32_t kSectionHeader = 40;
constexpr std::uint32_t kScnCode = 0x00000020;
constexpr std::uint32_t kScnExecute = 0x20000000;
constexpr std::uint32_t kScnInitializedData = 0x00000040;
constexpr std::uint32_t kScnRead = 0x40000000;
constexpr std::uint32_t kMaxEntries = 65536;
constexpr std::uint64_t kMaxFile = 1ull << 30;

template <class T>
bool readAt(std::string_view bytes, std::uint64_t offset, T& out) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return false;
    }
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return true;
}

template <class T>
void writeAt(std::string& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

std::uint32_t alignUp(std::uint64_t value, std::uint32_t alignment) {
    if (alignment == 0) {
        return static_cast<std::uint32_t>(value);
    }
    return static_cast<std::uint32_t>((value + alignment - 1) / alignment * alignment);
}

Error bad(std::wstring what) {
    return Error{ErrorCode::ParseError, std::move(what), L"PE"};
}

// Reads the resource tree. `rsrcOffset` = file offset of the directory root, `rsrcLimit` = end of
// the bytes the directory may live in; data is found through `rvaToOffset`.
class TreeReader {
public:
    TreeReader(std::string_view bytes, std::uint64_t root, std::uint64_t limit, const std::vector<PeSection>& sections)
        : m_bytes(bytes), m_root(root), m_limit(limit), m_sections(sections) {}

    Result<ResourceTree> read() {
        ResourceTree tree;
        std::vector<Entry> types;
        if (auto ok = directory(0, tree.meta, types); !ok) {
            return std::unexpected(ok.error());
        }
        for (const auto& t : types) {
            if (!t.subdir) {
                return std::unexpected(bad(L"resource type without a directory"));
            }
            ResourceType type{t.key, {}, {}};
            std::vector<Entry> names;
            if (auto ok = directory(t.offset, type.meta, names); !ok) {
                return std::unexpected(ok.error());
            }
            for (const auto& n : names) {
                if (!n.subdir) {
                    return std::unexpected(bad(L"resource name without a directory"));
                }
                ResourceName name{n.key, {}, {}};
                std::vector<Entry> languages;
                if (auto ok = directory(n.offset, name.meta, languages); !ok) {
                    return std::unexpected(ok.error());
                }
                for (const auto& l : languages) {
                    if (l.subdir || l.key.named()) {
                        return std::unexpected(bad(L"unexpected resource directory depth"));
                    }
                    auto leaf = data(l.offset);
                    if (!leaf) {
                        return std::unexpected(leaf.error());
                    }
                    leaf->language = l.key.id;
                    name.languages.push_back(std::move(*leaf));
                }
                type.names.push_back(std::move(name));
            }
            tree.types.push_back(std::move(type));
        }
        return tree;
    }

private:
    struct Entry {
        ResourceKey key;
        bool subdir = false;
        std::uint32_t offset = 0;
    };

    Result<void> directory(std::uint32_t offset, ResourceDirMeta& meta, std::vector<Entry>& entries) {
        if (!m_visited.insert(offset).second) {
            return std::unexpected(bad(L"resource directory loop"));
        }
        const std::uint64_t at = m_root + offset;
        if (at + 16 > m_limit) {
            return std::unexpected(bad(L"resource directory out of range"));
        }
        std::uint16_t named = 0;
        std::uint16_t ids = 0;
        readAt(m_bytes, at, meta.characteristics);
        readAt(m_bytes, at + 4, meta.timeDateStamp);
        readAt(m_bytes, at + 8, meta.majorVersion);
        readAt(m_bytes, at + 10, meta.minorVersion);
        readAt(m_bytes, at + 12, named);
        readAt(m_bytes, at + 14, ids);
        const std::uint32_t count = std::uint32_t{named} + ids;
        if (at + 16 + std::uint64_t{count} * 8 > m_limit) {
            return std::unexpected(bad(L"resource directory entries out of range"));
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t nameField = 0;
            std::uint32_t offsetField = 0;
            readAt(m_bytes, at + 16 + std::uint64_t{i} * 8, nameField);
            readAt(m_bytes, at + 20 + std::uint64_t{i} * 8, offsetField);
            Entry e;
            if (nameField & 0x80000000u) {
                const std::uint64_t s = m_root + (nameField & 0x7FFFFFFFu);
                std::uint16_t length = 0;
                if (!readAt(m_bytes, s, length) || s + 2 + std::uint64_t{length} * 2 > m_limit || length == 0) {
                    return std::unexpected(bad(L"resource name out of range"));
                }
                e.key.name.resize(length);
                std::memcpy(e.key.name.data(), m_bytes.data() + s + 2, std::size_t{length} * 2);
            } else {
                e.key.id = static_cast<std::uint16_t>(nameField & 0xFFFF);
            }
            e.subdir = (offsetField & 0x80000000u) != 0;
            e.offset = offsetField & 0x7FFFFFFFu;
            entries.push_back(std::move(e));
        }
        return {};
    }

    Result<ResourceLanguage> data(std::uint32_t offset) {
        const std::uint64_t at = m_root + offset;
        std::uint32_t rva = 0;
        std::uint32_t size = 0;
        ResourceLanguage leaf;
        if (at + 16 > m_limit || !readAt(m_bytes, at, rva) || !readAt(m_bytes, at + 4, size) ||
            !readAt(m_bytes, at + 8, leaf.codePage)) {
            return std::unexpected(bad(L"resource data entry out of range"));
        }
        if (size == 0) {
            return leaf;
        }
        for (const auto& s : m_sections) {
            const std::uint32_t span = std::max(s.virtualSize, s.rawSize);
            if (rva >= s.virtualAddress && rva - s.virtualAddress < span) {
                const std::uint64_t inSection = rva - s.virtualAddress;
                if (inSection + size > s.rawSize || std::uint64_t{s.rawPointer} + inSection + size > m_bytes.size()) {
                    return std::unexpected(bad(L"resource data out of range"));
                }
                leaf.data.assign(m_bytes.data() + s.rawPointer + inSection, size);
                return leaf;
            }
        }
        return std::unexpected(bad(L"resource data outside every section"));
    }

    std::string_view m_bytes;
    std::uint64_t m_root;
    std::uint64_t m_limit;
    const std::vector<PeSection>& m_sections;
    std::set<std::uint32_t> m_visited;
};

// The tree laid out at `baseRva`: directory tables, name strings, data entries, data.
std::string serialize(const ResourceTree& tree, std::uint32_t baseRva) {
    // Named entries before ids, as the loader's binary search expects; order within kept.
    auto ordered = [](const auto& list) {
        std::vector<const std::remove_cvref_t<decltype(list.front())>*> out;
        for (const auto& e : list) {
            if (e.key.named()) {
                out.push_back(&e);
            }
        }
        for (const auto& e : list) {
            if (!e.key.named()) {
                out.push_back(&e);
            }
        }
        return out;
    };
    const auto types = ordered(tree.types);
    std::uint32_t dirBytes = 16 + 8 * static_cast<std::uint32_t>(types.size());
    std::uint32_t leafCount = 0;
    std::uint32_t stringBytes = 0;
    auto addString = [&](const ResourceKey& k) {
        if (k.named()) {
            stringBytes += 2 + 2 * static_cast<std::uint32_t>(k.name.size());
        }
    };
    for (const auto* t : types) {
        addString(t->key);
        dirBytes += 16 + 8 * static_cast<std::uint32_t>(t->names.size());
        for (const auto& n : t->names) {
            addString(n.key);
            dirBytes += 16 + 8 * static_cast<std::uint32_t>(n.languages.size());
            leafCount += static_cast<std::uint32_t>(n.languages.size());
        }
    }
    const std::uint32_t stringStart = dirBytes;
    const std::uint32_t entryStart = alignUp(stringStart + stringBytes, 4);
    std::uint32_t dataStart = alignUp(entryStart + leafCount * 16, 8);
    std::uint32_t total = dataStart;
    for (const auto* t : types) {
        for (const auto& n : t->names) {
            for (const auto& l : n.languages) {
                total = alignUp(total + l.data.size(), 8);
            }
        }
    }
    std::string out(total, '\0');
    std::uint32_t nextDir = 0;
    std::uint32_t nextString = stringStart;
    std::uint32_t nextEntry = entryStart;
    std::uint32_t nextData = dataStart;

    auto writeDir = [&](const ResourceDirMeta& meta, std::uint16_t named, std::uint16_t ids) {
        const std::uint32_t at = nextDir;
        writeAt(out, at, meta.characteristics);
        writeAt(out, at + 4, meta.timeDateStamp);
        writeAt(out, at + 8, meta.majorVersion);
        writeAt(out, at + 10, meta.minorVersion);
        writeAt(out, at + 12, named);
        writeAt(out, at + 14, ids);
        nextDir += 16 + 8 * (std::uint32_t{named} + ids);
        return at;
    };
    auto nameField = [&](const ResourceKey& k) -> std::uint32_t {
        if (!k.named()) {
            return k.id;
        }
        const std::uint32_t at = nextString;
        writeAt(out, at, static_cast<std::uint16_t>(k.name.size()));
        std::memcpy(out.data() + at + 2, k.name.data(), k.name.size() * 2);
        nextString += 2 + 2 * static_cast<std::uint32_t>(k.name.size());
        return 0x80000000u | at;
    };
    auto countNamed = [](const auto& list) {
        std::uint16_t n = 0;
        for (const auto* e : list) {
            n += e->key.named() ? 1 : 0;
        }
        return n;
    };

    // Breadth first: root, then every type's table, then every name's table.
    const std::uint16_t typeNamed = countNamed(types);
    const std::uint32_t root = writeDir(tree.meta, typeNamed, static_cast<std::uint16_t>(types.size() - typeNamed));
    std::vector<std::uint32_t> typeDirs;
    for (const auto* t : types) {
        const auto names = ordered(t->names);
        const std::uint16_t named = countNamed(names);
        typeDirs.push_back(writeDir(t->meta, named, static_cast<std::uint16_t>(names.size() - named)));
    }
    for (std::size_t ti = 0; ti < types.size(); ++ti) {
        writeAt(out, root + 16 + 8 * ti, nameField(types[ti]->key));
        writeAt(out, root + 20 + 8 * ti, 0x80000000u | typeDirs[ti]);
    }
    for (std::size_t ti = 0; ti < types.size(); ++ti) {
        const auto names = ordered(types[ti]->names);
        for (std::size_t ni = 0; ni < names.size(); ++ni) {
            const auto& languages = names[ni]->languages;
            const std::uint32_t dir = writeDir(names[ni]->meta, 0, static_cast<std::uint16_t>(languages.size()));
            writeAt(out, typeDirs[ti] + 16 + 8 * ni, nameField(names[ni]->key));
            writeAt(out, typeDirs[ti] + 20 + 8 * ni, 0x80000000u | dir);
            for (std::size_t li = 0; li < languages.size(); ++li) {
                const auto& leaf = languages[li];
                writeAt(out, dir + 16 + 8 * li, static_cast<std::uint32_t>(leaf.language));
                writeAt(out, dir + 20 + 8 * li, nextEntry);
                writeAt(out, nextEntry, baseRva + nextData);
                writeAt(out, nextEntry + 4, static_cast<std::uint32_t>(leaf.data.size()));
                writeAt(out, nextEntry + 8, leaf.codePage);
                writeAt(out, nextEntry + 12, std::uint32_t{0});
                nextEntry += 16;
                std::memcpy(out.data() + nextData, leaf.data.data(), leaf.data.size());
                nextData = alignUp(nextData + leaf.data.size(), 8);
            }
        }
    }
    return out;
}

} // namespace

std::wstring ResourceKey::text() const {
    return named() ? name : std::format(L"#{}", id);
}

ResourceType* ResourceTree::type(std::uint16_t id) {
    for (auto& t : types) {
        if (!t.key.named() && t.key.id == id) {
            return &t;
        }
    }
    return nullptr;
}

const ResourceType* ResourceTree::type(std::uint16_t id) const {
    return const_cast<ResourceTree*>(this)->type(id);
}

ResourceType& ResourceTree::ensureType(std::uint16_t id) {
    if (auto* t = type(id)) {
        return *t;
    }
    auto at = std::ranges::find_if(types, [&](const ResourceType& t) { return !t.key.named() && t.key.id > id; });
    return *types.insert(at, ResourceType{ResourceKey{id, {}}, {}, {}});
}

std::size_t ResourceTree::leafCount() const {
    std::size_t n = 0;
    for (const auto& t : types) {
        for (const auto& name : t.names) {
            n += name.languages.size();
        }
    }
    return n;
}

ResourceName& ensureName(ResourceType& type, std::uint16_t id) {
    for (auto& n : type.names) {
        if (!n.key.named() && n.key.id == id) {
            return n;
        }
    }
    auto at = std::ranges::find_if(type.names, [&](const ResourceName& n) { return !n.key.named() && n.key.id > id; });
    return *type.names.insert(at, ResourceName{ResourceKey{id, {}}, {}, {}});
}

bool PeImage::hasCode() const noexcept {
    return std::ranges::any_of(m_sections, [](const PeSection& s) {
        return (s.characteristics & (kScnCode | kScnExecute)) != 0;
    });
}

Result<PeImage> PeImage::parse(std::string bytes) {
    if (bytes.size() > kMaxFile) {
        return std::unexpected(bad(L"file too large"));
    }
    PeImage pe;
    std::uint16_t mz = 0;
    std::uint32_t lfanew = 0;
    std::uint32_t signature = 0;
    if (!readAt(bytes, 0, mz) || mz != 0x5A4D || !readAt(bytes, 0x3C, lfanew) || !readAt(bytes, lfanew, signature) ||
        signature != 0x00004550) {
        return std::unexpected(bad(L"not a PE file"));
    }
    pe.m_ntOffset = lfanew;
    std::uint16_t sectionCount = 0;
    std::uint16_t optionalSize = 0;
    readAt(bytes, std::uint64_t{lfanew} + 4, pe.m_machine);
    readAt(bytes, std::uint64_t{lfanew} + 6, sectionCount);
    if (!readAt(bytes, std::uint64_t{lfanew} + 20, optionalSize)) {
        return std::unexpected(bad(L"truncated PE header"));
    }
    pe.m_optionalOffset = lfanew + 24;
    std::uint16_t magic = 0;
    if (!readAt(bytes, pe.m_optionalOffset, magic) || (magic != 0x10B && magic != 0x20B)) {
        return std::unexpected(bad(L"unknown optional header"));
    }
    pe.m_is64 = magic == 0x20B;
    const std::uint32_t dirOffset = pe.m_optionalOffset + (pe.m_is64 ? 112 : 96);
    const std::uint32_t countOffset = pe.m_optionalOffset + (pe.m_is64 ? 108 : 92);
    std::uint32_t dirCount = 0;
    readAt(bytes, pe.m_optionalOffset + 32, pe.m_sectionAlignment);
    readAt(bytes, pe.m_optionalOffset + 36, pe.m_fileAlignment);
    readAt(bytes, pe.m_optionalOffset + 60, pe.m_sizeOfHeaders);
    if (!readAt(bytes, countOffset, dirCount) || optionalSize < (dirOffset - pe.m_optionalOffset) + 8 * std::min(dirCount, 16u)) {
        return std::unexpected(bad(L"truncated optional header"));
    }
    if (dirCount > 2) {
        readAt(bytes, dirOffset + 2 * 8, pe.m_rsrcRva);
        readAt(bytes, dirOffset + 2 * 8 + 4, pe.m_rsrcSize);
    }
    if (dirCount > 4) {
        readAt(bytes, dirOffset + 4 * 8, pe.m_securityOffset);
        readAt(bytes, dirOffset + 4 * 8 + 4, pe.m_securitySize);
    }
    pe.m_sectionTableOffset = pe.m_optionalOffset + optionalSize;
    if (sectionCount == 0 || sectionCount > 96 ||
        std::uint64_t{pe.m_sectionTableOffset} + std::uint64_t{sectionCount} * kSectionHeader > bytes.size()) {
        return std::unexpected(bad(L"bad section table"));
    }
    for (std::uint16_t i = 0; i < sectionCount; ++i) {
        const std::uint64_t at = pe.m_sectionTableOffset + std::uint64_t{i} * kSectionHeader;
        PeSection s;
        char name[9]{};
        std::memcpy(name, bytes.data() + at, 8);
        s.name = name;
        readAt(bytes, at + 8, s.virtualSize);
        readAt(bytes, at + 12, s.virtualAddress);
        readAt(bytes, at + 16, s.rawSize);
        readAt(bytes, at + 20, s.rawPointer);
        readAt(bytes, at + 36, s.characteristics);
        if (std::uint64_t{s.rawPointer} + s.rawSize > bytes.size()) {
            return std::unexpected(bad(L"section outside the file"));
        }
        pe.m_sections.push_back(std::move(s));
    }
    if (pe.m_rsrcRva != 0) {
        const auto section = std::ranges::find_if(pe.m_sections, [&](const PeSection& s) {
            return pe.m_rsrcRva >= s.virtualAddress && pe.m_rsrcRva - s.virtualAddress < std::max(s.virtualSize, s.rawSize);
        });
        if (section == pe.m_sections.end()) {
            return std::unexpected(bad(L"resource directory outside every section"));
        }
        const std::uint64_t root = std::uint64_t{section->rawPointer} + (pe.m_rsrcRva - section->virtualAddress);
        const std::uint64_t limit = std::uint64_t{section->rawPointer} + section->rawSize;
        auto tree = TreeReader(bytes, root, limit, pe.m_sections).read();
        if (!tree) {
            return std::unexpected(tree.error());
        }
        if (tree->leafCount() > kMaxEntries) {
            return std::unexpected(bad(L"too many resources"));
        }
        pe.m_tree = std::move(*tree);
    }
    pe.m_bytes = std::move(bytes);
    return pe;
}

Result<std::string> PeImage::build() const {
    if (m_rsrcRva == 0) {
        return std::unexpected(bad(L"the file has no resources"));
    }
    const std::uint32_t dirOffset = m_optionalOffset + (m_is64 ? 112 : 96);
    const std::uint32_t checksumOffset = m_optionalOffset + 64;
    const std::uint32_t sizeOfImageOffset = m_optionalOffset + 56;

    // Where the sections end, in the file and in memory.
    std::uint64_t rawEnd = m_sizeOfHeaders;
    std::uint64_t virtualEnd = 0;
    std::size_t lastRaw = 0;
    std::size_t lastVirtual = 0;
    for (std::size_t i = 0; i < m_sections.size(); ++i) {
        const auto& s = m_sections[i];
        if (std::uint64_t{s.rawPointer} + s.rawSize >= rawEnd && s.rawSize > 0) {
            rawEnd = std::uint64_t{s.rawPointer} + s.rawSize;
            lastRaw = i;
        }
        const std::uint64_t vEnd = std::uint64_t{s.virtualAddress} + std::max(s.virtualSize, s.rawSize);
        if (vEnd >= virtualEnd) {
            virtualEnd = vEnd;
            lastVirtual = i;
        }
    }
    // Data after the last section: only a signature may be there (and it goes).
    if (rawEnd < m_bytes.size()) {
        const bool onlySignature = m_securitySize != 0 && m_securityOffset >= rawEnd &&
                                   std::uint64_t{m_securityOffset} + m_securitySize >= m_bytes.size() &&
                                   std::all_of(m_bytes.begin() + static_cast<std::ptrdiff_t>(rawEnd),
                                               m_bytes.begin() + static_cast<std::ptrdiff_t>(m_securityOffset),
                                               [](char c) { return c == '\0'; });
        if (!onlySignature) {
            return std::unexpected(bad(L"the file has data after its last section"));
        }
    }

    const auto rsrc = std::ranges::find_if(m_sections, [&](const PeSection& s) {
        return m_rsrcRva >= s.virtualAddress && m_rsrcRva - s.virtualAddress < std::max(s.virtualSize, s.rawSize);
    });
    const std::size_t rsrcIndex = static_cast<std::size_t>(rsrc - m_sections.begin());
    const bool inPlace = rsrc->virtualAddress == m_rsrcRva && rsrcIndex == lastRaw && rsrcIndex == lastVirtual &&
                         rsrcIndex == m_sections.size() - 1;

    std::string out;
    std::uint32_t sectionHeader = 0;
    std::uint32_t sectionVa = 0;
    std::uint32_t rawPointer = 0;
    if (inPlace) {
        sectionHeader = m_sectionTableOffset + static_cast<std::uint32_t>(rsrcIndex) * kSectionHeader;
        sectionVa = rsrc->virtualAddress;
        rawPointer = rsrc->rawPointer;
        out.assign(m_bytes.data(), rawPointer);
    } else {
        // A new section after the last one: room for its header is needed.
        const std::uint32_t tableEnd = m_sectionTableOffset + static_cast<std::uint32_t>(m_sections.size()) * kSectionHeader;
        std::uint32_t firstRaw = m_sizeOfHeaders;
        for (const auto& s : m_sections) {
            if (s.rawSize > 0) {
                firstRaw = std::min(firstRaw, s.rawPointer);
            }
        }
        if (tableEnd + kSectionHeader > std::min(m_sizeOfHeaders, firstRaw)) {
            return std::unexpected(bad(L"no room for another section header"));
        }
        if (std::any_of(m_bytes.begin() + tableEnd, m_bytes.begin() + tableEnd + kSectionHeader, [](char c) { return c != '\0'; })) {
            return std::unexpected(bad(L"the bytes after the section table are in use"));
        }
        sectionHeader = tableEnd;
        sectionVa = alignUp(virtualEnd, m_sectionAlignment);
        rawPointer = alignUp(rawEnd, m_fileAlignment);
        out.assign(m_bytes.data(), static_cast<std::size_t>(rawEnd));
        out.resize(rawPointer, '\0');
        const char name[8] = {'.', 'r', 's', 'r', 'c', '2', 0, 0};
        std::memcpy(out.data() + sectionHeader, name, 8);
        writeAt(out, sectionHeader + 36, kScnInitializedData | kScnRead);
        std::uint16_t count = 0;
        readAt(out, m_ntOffset + 6, count);
        writeAt(out, m_ntOffset + 6, static_cast<std::uint16_t>(count + 1));
    }
    const std::string data = serialize(m_tree, sectionVa);
    const std::uint32_t rawSize = alignUp(data.size(), m_fileAlignment);
    out.append(data);
    out.resize(std::size_t{rawPointer} + rawSize, '\0');
    writeAt(out, sectionHeader + 8, static_cast<std::uint32_t>(data.size()));
    writeAt(out, sectionHeader + 12, sectionVa);
    writeAt(out, sectionHeader + 16, rawSize);
    writeAt(out, sectionHeader + 20, rawPointer);
    writeAt(out, sizeOfImageOffset, alignUp(std::uint64_t{sectionVa} + data.size(), m_sectionAlignment));
    writeAt(out, dirOffset + 2 * 8, sectionVa);
    writeAt(out, dirOffset + 2 * 8 + 4, static_cast<std::uint32_t>(data.size()));
    // The signature covered the old bytes; it is gone with them.
    writeAt(out, dirOffset + 4 * 8, std::uint32_t{0});
    writeAt(out, dirOffset + 4 * 8 + 4, std::uint32_t{0});
    // SizeOfInitializedData follows the sections' raw sizes.
    std::uint32_t initialized = 0;
    std::uint16_t count = 0;
    readAt(out, m_ntOffset + 6, count);
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint32_t flags = 0;
        std::uint32_t size = 0;
        readAt(out, m_sectionTableOffset + std::uint64_t{i} * kSectionHeader + 36, flags);
        readAt(out, m_sectionTableOffset + std::uint64_t{i} * kSectionHeader + 16, size);
        if (flags & kScnInitializedData) {
            initialized += size;
        }
    }
    writeAt(out, m_optionalOffset + 8, initialized);
    writeAt(out, checksumOffset, peChecksum(out, checksumOffset));
    return out;
}

std::uint32_t peChecksum(std::string_view file, std::uint32_t checksumOffset) {
    std::uint64_t sum = 0;
    const std::size_t words = file.size() / 2;
    for (std::size_t i = 0; i < words; ++i) {
        if (i * 2 == checksumOffset || i * 2 == checksumOffset + 2) {
            continue;
        }
        std::uint16_t w = 0;
        std::memcpy(&w, file.data() + i * 2, 2);
        sum += w;
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    if (file.size() % 2) {
        sum += static_cast<std::uint8_t>(file.back());
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<std::uint32_t>(sum + file.size());
}

} // namespace wl::core
