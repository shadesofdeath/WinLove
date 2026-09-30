#pragma once
// Native WIM/ESD header + XML metadata reader (no wimgapi, no DISM, no admin).
// Works on any ByteSource — a .wim on disk or install.esd inside an ISO.
// Layout: WIMHEADER_V1_PACKED (208 bytes), XML resource = UTF-16LE document (docs/ENGINE.md).
#include "core/image/ImageInfo.h"
#include "core/io/ByteSource.h"

#include <string>
#include <vector>

namespace wl::core {

enum class WimCompression : std::uint8_t { None, Xpress, Lzx, Lzms, Unknown };

struct WimHeader {
    std::uint32_t version = 0;
    std::uint32_t flags = 0;
    std::uint32_t chunkSize = 0;
    std::uint16_t partNumber = 1;
    std::uint16_t totalParts = 1;
    std::uint32_t imageCount = 0;
    std::uint32_t bootIndex = 0;
    std::uint64_t xmlOffset = 0;
    std::uint64_t xmlSize = 0;
    std::uint64_t lookupOffset = 0; // the table of every stream: location, sizes, SHA-1 (WimVerify)
    std::uint64_t lookupSize = 0;
    bool lookupCompressed = false;
    WimCompression compression = WimCompression::None;
    bool solid = false; // ESD-style solid resources (LZMS)
};

struct WimFile {
    WimHeader header;
    std::vector<ImageInfo> images;
};

[[nodiscard]] const wchar_t* compressionName(WimCompression compression) noexcept;

[[nodiscard]] Result<WimHeader> readWimHeader(const ByteSource& source);
// Parses the XML metadata block (UTF-16LE, optional BOM) into image descriptions.
[[nodiscard]] Result<std::vector<ImageInfo>> parseWimXml(std::u16string_view xml);
[[nodiscard]] Result<WimFile> readWim(const ByteSource& source);

} // namespace wl::core
