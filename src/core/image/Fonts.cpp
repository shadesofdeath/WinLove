#include "core/image/Fonts.h"

#include "base/File.h"
#include "base/Text.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

namespace wl::core {

namespace {

std::uint16_t be16(std::string_view b, std::size_t at) {
    return static_cast<std::uint16_t>((static_cast<std::uint8_t>(b[at]) << 8) | static_cast<std::uint8_t>(b[at + 1]));
}
std::uint32_t be32(std::string_view b, std::size_t at) {
    return (static_cast<std::uint32_t>(be16(b, at)) << 16) | be16(b, at + 2);
}

// One font of the file starts at `offset` (its table directory). Its full name and whether the
// outlines are CFF.
Result<std::pair<std::wstring, bool>> readOne(std::string_view b, std::size_t offset) {
    if (offset + 12 > b.size()) {
        return fail(ErrorCode::ParseError, L"font table directory is cut off");
    }
    const std::uint32_t version = be32(b, offset);
    const bool cff = version == 0x4F54544F; // 'OTTO'
    if (!cff && version != 0x00010000 && version != 0x74727565 /*'true'*/) {
        return fail(ErrorCode::ParseError, L"not a TrueType / OpenType font");
    }
    const std::uint16_t tables = be16(b, offset + 4);
    if (offset + 12 + std::size_t{tables} * 16 > b.size()) {
        return fail(ErrorCode::ParseError, L"font table directory is cut off");
    }
    for (std::uint16_t t = 0; t < tables; ++t) {
        const std::size_t entry = offset + 12 + std::size_t{t} * 16;
        if (b.substr(entry, 4) != "name") {
            continue;
        }
        const std::size_t at = be32(b, entry + 8);
        const std::size_t length = be32(b, entry + 12);
        if (at + 6 > b.size() || length > b.size() - at) {
            return fail(ErrorCode::ParseError, L"font name table is cut off");
        }
        const std::uint16_t count = be16(b, at + 2);
        const std::size_t strings = at + be16(b, at + 4);
        // Best record: Windows (3) Unicode BMP (1) English US (0x409), then any Windows, then Mac Roman.
        int bestScore = -1;
        std::wstring best;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::size_t r = at + 6 + std::size_t{i} * 12;
            if (r + 12 > b.size()) {
                break;
            }
            const std::uint16_t platform = be16(b, r);
            const std::uint16_t encoding = be16(b, r + 2);
            const std::uint16_t language = be16(b, r + 4);
            const std::uint16_t nameId = be16(b, r + 6);
            const std::size_t len = be16(b, r + 8);
            const std::size_t off = strings + be16(b, r + 10);
            if (nameId != 4 || off + len > b.size()) {
                continue;
            }
            int score = -1;
            std::wstring text;
            if (platform == 3 && (encoding == 1 || encoding == 10 || encoding == 0)) {
                score = language == 0x409 ? 3 : 2;
                for (std::size_t k = 0; k + 1 < len; k += 2) {
                    text.push_back(static_cast<wchar_t>(be16(b, off + k)));
                }
            } else if (platform == 1 && encoding == 0) {
                score = 1;
                for (std::size_t k = 0; k < len; ++k) {
                    text.push_back(static_cast<wchar_t>(static_cast<std::uint8_t>(b[off + k])));
                }
            }
            if (score > bestScore && !text.empty()) {
                bestScore = score;
                best = std::move(text);
            }
        }
        if (best.empty()) {
            return fail(ErrorCode::ParseError, L"the font has no full name");
        }
        return std::pair{best, cff};
    }
    return fail(ErrorCode::ParseError, L"the font has no name table");
}

} // namespace

std::wstring FontInfo::registryName() const {
    std::wstring name;
    for (const auto& n : names) {
        name += (name.empty() ? L"" : L" & ") + n;
    }
    return name + (postscript ? L" (OpenType)" : L" (TrueType)");
}

bool isFontFile(const std::filesystem::path& file) {
    const std::wstring ext = text::lower(file.extension().wstring());
    return ext == L".ttf" || ext == L".otf" || ext == L".ttc";
}

Result<FontInfo> parseFont(std::string_view bytes) {
    if (bytes.size() < 12) {
        return fail(ErrorCode::ParseError, L"not a font");
    }
    FontInfo info;
    if (bytes.substr(0, 4) == "ttcf") {
        const std::uint32_t fonts = be32(bytes, 8);
        if (fonts == 0 || fonts > 256 || 12 + std::size_t{fonts} * 4 > bytes.size()) {
            return fail(ErrorCode::ParseError, L"font collection header is broken");
        }
        for (std::uint32_t i = 0; i < fonts; ++i) {
            auto one = readOne(bytes, be32(bytes, 12 + std::size_t{i} * 4));
            if (!one) {
                return std::unexpected(one.error());
            }
            if (std::ranges::find(info.names, one->first) == info.names.end()) {
                info.names.push_back(one->first);
            }
            info.postscript = info.postscript || one->second;
        }
        info.postscript = false; // Windows lists collections as TrueType
        return info;
    }
    auto one = readOne(bytes, 0);
    if (!one) {
        return std::unexpected(one.error());
    }
    info.names.push_back(one->first);
    info.postscript = one->second;
    return info;
}

Result<FontInfo> readFontInfo(const std::filesystem::path& file) {
    const auto bytes = readFileBytes(file);
    if (!bytes) {
        return fail(ErrorCode::NotFound, L"font file not found", file.wstring());
    }
    auto info = parseFont(*bytes);
    if (!info) {
        Error e = info.error();
        e.context = file.wstring();
        return std::unexpected(e);
    }
    return info;
}

std::wstring fontFileName(const std::filesystem::path& source) {
    std::wstring name = source.filename().wstring();
    for (auto& c : name) {
        const bool plain = c < 128 && (std::iswalnum(c) != 0 || c == L'-' || c == L'_' || c == L'.' || c == L' ');
        if (!plain) {
            c = L'_';
        }
    }
    if (name.empty() || name.front() == L'.') {
        name = L"font" + name;
    }
    return name;
}

} // namespace wl::core
