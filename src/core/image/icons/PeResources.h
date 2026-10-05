#pragma once
// D-068: the resource section of a PE file (.dll, .exe, .cpl and the resource-only .mun files of
// Windows\SystemResources), read and written by our own code — no LoadLibrary, no UpdateResource:
// the file may be an offline image's, of another architecture, and it must come back byte-exact
// except for what was changed.
//
// Reading: headers and section table are bounds-checked; the resource directory is the usual
// three levels (type → name → language) of IMAGE_RESOURCE_DIRECTORY tables. Order, directory
// metadata, code pages and data are kept as they are.
//
// Writing (build): the tree is laid out again (directory tables, name strings, data entries,
// data 8-byte aligned). When the resource section is the last one of the file it is rewritten in
// place (grown or shrunk); otherwise a new section ".rsrc2" is appended and the resource data
// directory points at it (the old one stays as dead bytes). An embedded signature (the security
// directory) no longer matches the file and is dropped; any other data after the last section is
// refused. SizeOfImage, the section sizes and the PE checksum are updated.
#include "base/Result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wl::core {

struct ResourceKey {
    std::uint16_t id = 0;
    std::wstring name; // non-empty: a named entry (then id is unused)
    [[nodiscard]] bool named() const noexcept { return !name.empty(); }
    [[nodiscard]] bool operator==(const ResourceKey&) const = default;
    [[nodiscard]] std::wstring text() const; // "#3" or the name
};

struct ResourceDirMeta {
    std::uint32_t characteristics = 0;
    std::uint32_t timeDateStamp = 0;
    std::uint16_t majorVersion = 0;
    std::uint16_t minorVersion = 0;
};

struct ResourceLanguage {
    std::uint16_t language = 0;
    std::uint32_t codePage = 0;
    std::string data;
};

struct ResourceName {
    ResourceKey key;
    ResourceDirMeta meta;
    std::vector<ResourceLanguage> languages;
};

struct ResourceType {
    ResourceKey key;
    ResourceDirMeta meta;
    std::vector<ResourceName> names;
};

struct ResourceTree {
    ResourceDirMeta meta;
    std::vector<ResourceType> types;

    [[nodiscard]] ResourceType* type(std::uint16_t id);
    [[nodiscard]] const ResourceType* type(std::uint16_t id) const;
    // Adds an id type in its sorted place (ids after names, ascending) when it is missing.
    ResourceType& ensureType(std::uint16_t id);
    [[nodiscard]] std::size_t leafCount() const;
};

// Inserts / finds an id entry of a directory in sorted place (named entries first, then ids ascending).
ResourceName& ensureName(ResourceType& type, std::uint16_t id);

struct PeSection {
    std::string name;
    std::uint32_t virtualAddress = 0;
    std::uint32_t virtualSize = 0;
    std::uint32_t rawPointer = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;
};

class PeImage {
public:
    [[nodiscard]] static Result<PeImage> parse(std::string bytes);

    [[nodiscard]] bool is64() const noexcept { return m_is64; }
    [[nodiscard]] std::uint16_t machine() const noexcept { return m_machine; }
    [[nodiscard]] const std::vector<PeSection>& sections() const noexcept { return m_sections; }
    // Any section with code or execute rights: replacing its resources changes a program's file
    // (signature / catalog hash), not just a resource container.
    [[nodiscard]] bool hasCode() const noexcept;
    [[nodiscard]] bool hasEmbeddedSignature() const noexcept { return m_securitySize != 0; }
    [[nodiscard]] bool hasResources() const noexcept { return m_rsrcRva != 0; }

    [[nodiscard]] const ResourceTree& resources() const noexcept { return m_tree; }
    [[nodiscard]] ResourceTree& resources() noexcept { return m_tree; }

    // The whole file with the current tree (see the header comment).
    [[nodiscard]] Result<std::string> build() const;

    [[nodiscard]] const std::string& bytes() const noexcept { return m_bytes; }

private:
    std::string m_bytes;
    bool m_is64 = false;
    std::uint16_t m_machine = 0;
    std::uint32_t m_ntOffset = 0;
    std::uint32_t m_optionalOffset = 0;
    std::uint32_t m_sectionTableOffset = 0;
    std::uint32_t m_sizeOfHeaders = 0;
    std::uint32_t m_fileAlignment = 0;
    std::uint32_t m_sectionAlignment = 0;
    std::uint32_t m_rsrcRva = 0;
    std::uint32_t m_rsrcSize = 0;
    std::uint32_t m_securityOffset = 0;
    std::uint32_t m_securitySize = 0;
    std::vector<PeSection> m_sections;
    ResourceTree m_tree;
};

// The PE checksum (CheckSumMappedFile's algorithm) of `file`, its CheckSum field taken as zero.
[[nodiscard]] std::uint32_t peChecksum(std::string_view file, std::uint32_t checksumOffset);

} // namespace wl::core
