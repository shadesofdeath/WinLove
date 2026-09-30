#include "core/image/WimFile.h"

#include "base/Utf8.h"

#include <pugixml.hpp>

#include <cstring>
#include <format>

namespace wl::core {

namespace {

constexpr std::size_t kHeaderSize = 208;
constexpr char kMagic[8] = {'M', 'S', 'W', 'I', 'M', 0, 0, 0};

// FLAG_HEADER_* (wimlib / MS-WIM docs)
constexpr std::uint32_t kFlagCompression = 0x00000002;
constexpr std::uint32_t kFlagCompressXpress = 0x00020000;
constexpr std::uint32_t kFlagCompressLzx = 0x00040000;
constexpr std::uint32_t kFlagCompressLzms = 0x00080000;
constexpr std::uint32_t kResourceCompressed = 0x04;
constexpr std::uint32_t kVersionSolid = 0x00000E00; // "version 3584": ESD with solid resources

template <class T>
T le(const std::byte* p) {
    T value{};
    std::memcpy(&value, p, sizeof(T));
    return value;
}

std::wstring text(const pugi::xml_node& node, const char* child) {
    return utf8::toWide(node.child(child).text().as_string());
}

std::uint64_t number(const pugi::xml_node& node, const char* child) {
    return node.child(child).text().as_ullong();
}

// <CREATIONTIME> etc. are fine as-is; sizes may be written in hex ("0x...") by some tools.
std::uint64_t parseSize(const pugi::xml_node& node) {
    const char* s = node.text().as_string();
    if (std::strncmp(s, "0x", 2) == 0 || std::strncmp(s, "0X", 2) == 0) {
        return std::strtoull(s + 2, nullptr, 16);
    }
    return std::strtoull(s, nullptr, 10);
}

Architecture architectureFrom(int code) {
    // PROCESSOR_ARCHITECTURE_* values used by the WIM XML <ARCH>.
    switch (code) {
    case 0: return Architecture::X86;
    case 5: return Architecture::Arm;
    case 9: return Architecture::X64;
    case 12: return Architecture::Arm64;
    default: return Architecture::Unknown;
    }
}

} // namespace

std::wstring ImageInfo::versionString() const {
    return std::format(L"{}.{}.{}.{}", major, minor, build, spBuild);
}

const wchar_t* architectureName(Architecture arch) noexcept {
    switch (arch) {
    case Architecture::X86: return L"x86";
    case Architecture::X64: return L"x64";
    case Architecture::Arm: return L"arm";
    case Architecture::Arm64: return L"arm64";
    case Architecture::Unknown: break;
    }
    return L"?";
}

const wchar_t* compressionName(WimCompression compression) noexcept {
    switch (compression) {
    case WimCompression::None: return L"none";
    case WimCompression::Xpress: return L"xpress";
    case WimCompression::Lzx: return L"lzx";
    case WimCompression::Lzms: return L"lzms";
    case WimCompression::Unknown: break;
    }
    return L"?";
}

Result<WimHeader> readWimHeader(const ByteSource& source) {
    if (source.size() < kHeaderSize) {
        return fail(ErrorCode::ParseError, L"file too small for a WIM header", L"WIM");
    }
    std::byte raw[kHeaderSize];
    if (auto r = source.read(0, raw); !r) {
        return std::unexpected(r.error());
    }
    if (std::memcmp(raw, kMagic, sizeof(kMagic)) != 0) {
        return fail(ErrorCode::ParseError, L"not a WIM/ESD file (bad magic)", L"WIM");
    }
    WimHeader h;
    h.version = le<std::uint32_t>(raw + 12);
    h.flags = le<std::uint32_t>(raw + 16);
    h.chunkSize = le<std::uint32_t>(raw + 20);
    h.partNumber = le<std::uint16_t>(raw + 40);
    h.totalParts = le<std::uint16_t>(raw + 42);
    h.imageCount = le<std::uint32_t>(raw + 44);
    // rhOffsetTable at 48: the lookup table (same RESHDR_DISK_SHORT layout as below).
    const auto lookupPacked = le<std::uint64_t>(raw + 48);
    h.lookupSize = lookupPacked & 0x00FFFFFFFFFFFFFFull;
    h.lookupCompressed = (static_cast<std::uint32_t>(lookupPacked >> 56) & kResourceCompressed) != 0;
    h.lookupOffset = le<std::uint64_t>(raw + 48 + 8);
    // rhXmlData: RESHDR_DISK_SHORT at 72 = { u56 size, u8 flags, u64 offset, u64 original size }
    const auto packed = le<std::uint64_t>(raw + 72);
    const auto xmlFlags = static_cast<std::uint32_t>(packed >> 56);
    h.xmlSize = le<std::uint64_t>(raw + 72 + 16); // original (uncompressed) size
    h.xmlOffset = le<std::uint64_t>(raw + 72 + 8);
    h.bootIndex = le<std::uint32_t>(raw + 120);
    h.solid = h.version == kVersionSolid;
    if ((h.flags & kFlagCompression) == 0) {
        h.compression = WimCompression::None;
    } else if (h.flags & kFlagCompressLzms) {
        h.compression = WimCompression::Lzms;
    } else if (h.flags & kFlagCompressLzx) {
        h.compression = WimCompression::Lzx;
    } else if (h.flags & kFlagCompressXpress) {
        h.compression = WimCompression::Xpress;
    } else {
        h.compression = WimCompression::Unknown;
    }
    if (xmlFlags & kResourceCompressed) {
        return fail(ErrorCode::Unsupported, L"compressed XML metadata", L"WIM");
    }
    if (h.xmlOffset + h.xmlSize > source.size() || h.xmlSize == 0 || h.xmlSize > (64u << 20)) {
        return fail(ErrorCode::ParseError, L"XML metadata out of range", L"WIM");
    }
    return h;
}

Result<std::vector<ImageInfo>> parseWimXml(std::u16string_view xml) {
    if (!xml.empty() && xml.front() == u'﻿') {
        xml.remove_prefix(1);
    }
    const std::string utf8 = utf8::fromWide(std::wstring_view(reinterpret_cast<const wchar_t*>(xml.data()), xml.size()));
    pugi::xml_document doc;
    const auto parsed = doc.load_buffer(utf8.data(), utf8.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parsed) {
        return fail(ErrorCode::ParseError, L"invalid WIM XML: " + utf8::toWide(parsed.description()), L"WIM");
    }
    std::vector<ImageInfo> images;
    for (const auto& node : doc.child("WIM").children("IMAGE")) {
        ImageInfo info;
        info.index = node.attribute("INDEX").as_int();
        info.name = text(node, "NAME");
        info.description = text(node, "DESCRIPTION");
        info.displayName = text(node, "DISPLAYNAME");
        info.totalBytes = parseSize(node.child("TOTALBYTES"));
        info.fileCount = number(node, "FILECOUNT");
        info.directoryCount = number(node, "DIRCOUNT");
        // <CREATIONTIME><HIGHPART>0x01DC…</HIGHPART><LOWPART>0x…</LOWPART></CREATIONTIME>
        const auto created = node.child("CREATIONTIME");
        info.creationTime = (parseSize(created.child("HIGHPART")) << 32) | (parseSize(created.child("LOWPART")) & 0xFFFFFFFFull);
        const auto modified = node.child("LASTMODIFICATIONTIME");
        info.modifiedTime = (parseSize(modified.child("HIGHPART")) << 32) | (parseSize(modified.child("LOWPART")) & 0xFFFFFFFFull);
        info.hardlinkBytes = parseSize(node.child("HARDLINKBYTES"));
        info.wimBoot = node.child("WIMBOOT").text().as_int() != 0;
        info.flags = text(node, "FLAGS");
        info.displayDescription = text(node, "DISPLAYDESCRIPTION");
        const auto windows = node.child("WINDOWS");
        info.architecture = architectureFrom(windows.child("ARCH").text().as_int(-1));
        info.editionId = text(windows, "EDITIONID");
        info.installationType = text(windows, "INSTALLATIONTYPE");
        info.productType = text(windows, "PRODUCTTYPE");
        info.productSuite = text(windows, "PRODUCTSUITE");
        info.systemRoot = text(windows, "SYSTEMROOT");
        info.imageState = text(windows.child("SERVICINGDATA"), "IMAGESTATE");
        const auto version = windows.child("VERSION");
        info.major = version.child("MAJOR").text().as_int();
        info.minor = version.child("MINOR").text().as_int();
        info.build = version.child("BUILD").text().as_int();
        info.spBuild = version.child("SPBUILD").text().as_int();
        info.spLevel = version.child("SPLEVEL").text().as_int();
        info.branch = text(version, "BRANCH");
        const auto languages = windows.child("LANGUAGES");
        for (const auto& language : languages.children("LANGUAGE")) {
            info.languages.push_back(utf8::toWide(language.text().as_string()));
        }
        info.defaultLanguage = text(languages, "DEFAULT");
        images.push_back(std::move(info));
    }
    return images;
}

Result<WimFile> readWim(const ByteSource& source) {
    auto header = readWimHeader(source);
    if (!header) {
        return std::unexpected(header.error());
    }
    std::u16string xml(static_cast<std::size_t>(header->xmlSize / 2), u'\0');
    if (auto r = source.read(header->xmlOffset, std::as_writable_bytes(std::span(xml))); !r) {
        return std::unexpected(r.error());
    }
    auto images = parseWimXml(xml);
    if (!images) {
        return std::unexpected(images.error());
    }
    if (images->size() != header->imageCount) {
        return fail(ErrorCode::ParseError,
                    std::format(L"header says {} images, XML has {}", header->imageCount, images->size()), L"WIM");
    }
    return WimFile{*header, std::move(*images)};
}

} // namespace wl::core
